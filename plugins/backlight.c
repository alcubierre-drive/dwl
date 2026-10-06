#include "backlight.h"
#include "redraw.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <poll.h>
#include <time.h>
#include <sys/time.h>
#include <systemd/sd-bus.h>

/** Tracks whether backlight-tooler.timer is active by listening to systemd's
 * PropertiesChanged signals on the user bus, instead of spawning
 * ``systemctl --user is-active`` every second. Runs on the plugins' shared
 * loop (poller.h): the bus fd and its timeout are event sources there, and
 * every call is asynchronous, so a slow bus never holds up the other
 * plugins. Everything but `enabled` is only touched on the loop thread, or
 * before it starts and after it has stopped; the first connection is made
 * from the loop too. If the bus is unavailable or the connection drops, the
 * last known state is kept and a reconnect is attempted every RETRY_USEC. */

/** the systemd user unit whose state the bar shows */
static const char unit_name[] = "backlight-tooler.timer";
#define RETRY_USEC (30 * 1000000ull)
/** how long a call may go unanswered before the connection counts as broken */
#define CALL_TIMEOUT_USEC (2 * 1000000ull)

struct awl_backlight_bus_t {
    awl_backlight_t* b;
    pa_mainloop_api* api;
    sd_bus* bus;
    sd_bus_slot* match;
    pa_io_event* io;
    pa_time_event* timeout; /* the bus's next timeout, NULL if none */
    pa_time_event* retry;   /* the next connection attempt, NULL if none */
    char* path;             /* the unit's object path */
    int failed;             /* set by callbacks, which can't drop the bus */
};

static void bus_update( awl_backlight_bus_t* s );

/** a timeval `usec` from now on the wall clock, which pa_mainloop_api takes */
static struct timeval in_usec( uint64_t usec ) {
    struct timeval tv;
    gettimeofday( &tv, NULL );
    tv.tv_sec += usec / 1000000;
    tv.tv_usec += usec % 1000000;
    if (tv.tv_usec >= 1000000) {
        tv.tv_sec++;
        tv.tv_usec -= 1000000;
    }
    return tv;
}

static void set_from_state( awl_backlight_t* b, const char* state ) {
    /* same semantics as `systemctl is-active`'s first byte: only "inactive"
     * counts as disabled */
    int enabled = (state && state[0] == 'i') ? 0 : 1;
    if (atomic_exchange( &b->enabled, enabled ) != enabled)
        awl_redraw_request();
}

/** a method reply: whether it is a proper one; marks the connection as
 * broken otherwise */
static int reply_ok( sd_bus_message* m, awl_backlight_bus_t* s ) {
    if (!sd_bus_message_is_method_error( m, NULL )) return 1;
    s->failed = 1;
    return 0;
}

/** Properties.Get(ActiveState) -> v */
static int on_state( sd_bus_message* m, void* userdata, sd_bus_error* ret_error ) {
    (void)ret_error;
    awl_backlight_bus_t* s = userdata;
    const char* state = NULL;
    if (!reply_ok( m, s )) return 0;
    if (sd_bus_message_read( m, "v", "s", &state ) < 0) {
        s->failed = 1;
        return 0;
    }
    set_from_state( s->b, state );
    return 0;
}

static void query_state( awl_backlight_bus_t* s ) {
    if (sd_bus_call_method_async( s->bus, NULL, "org.freedesktop.systemd1", s->path,
                "org.freedesktop.DBus.Properties", "Get", on_state, s,
                "ss", "org.freedesktop.systemd1.Unit", "ActiveState" ) < 0)
        s->failed = 1;
}

/** PropertiesChanged(s interface, a{sv} changed, as invalidated) */
static int on_properties_changed( sd_bus_message* m, void* userdata, sd_bus_error* ret_error ) {
    (void)ret_error;
    awl_backlight_bus_t* s = userdata;
    const char* iface = NULL;
    if (sd_bus_message_read( m, "s", &iface ) < 0) return 0;
    if (strcmp( iface, "org.freedesktop.systemd1.Unit" )) return 0;

    if (sd_bus_message_enter_container( m, 'a', "{sv}" ) < 0) return 0;
    while (sd_bus_message_enter_container( m, 'e', "sv" ) > 0) {
        const char* key = NULL;
        if (sd_bus_message_read( m, "s", &key ) < 0) return 0;
        if (!strcmp( key, "ActiveState" )) {
            const char* state = NULL;
            if (sd_bus_message_read( m, "v", "s", &state ) < 0) return 0;
            set_from_state( s->b, state );
        } else if (sd_bus_message_skip( m, "v" ) < 0) {
            return 0;
        }
        if (sd_bus_message_exit_container( m ) < 0) return 0;
    }
    if (sd_bus_message_exit_container( m ) < 0) return 0;

    char** invalidated = NULL;
    if (sd_bus_message_read_strv( m, &invalidated ) >= 0 && invalidated) {
        int requery = 0;
        for (char** i = invalidated; *i; ++i) {
            if (!strcmp( *i, "ActiveState" )) requery = 1;
            free( *i );
        }
        free( invalidated );
        if (requery) query_state( s );
    }
    return 0;
}

static int on_subscribed( sd_bus_message* m, void* userdata, sd_bus_error* ret_error ) {
    (void)ret_error;
    reply_ok( m, userdata );
    return 0;
}

/** LoadUnit(s) -> o */
static int on_unit( sd_bus_message* m, void* userdata, sd_bus_error* ret_error ) {
    (void)ret_error;
    awl_backlight_bus_t* s = userdata;
    const char* path = NULL;
    if (!reply_ok( m, s )) return 0;
    if (sd_bus_message_read( m, "o", &path ) < 0 || !(s->path = strdup( path ))) {
        s->failed = 1;
        return 0;
    }
    /* The bus handles these in order, so the match is in place before the
     * state is read and no change can slip through in between. systemd
     * only emits unit PropertiesChanged signals while at least one client
     * is subscribed; the subscription ends with our connection. */
    if (sd_bus_match_signal_async( s->bus, &s->match, "org.freedesktop.systemd1", s->path,
                "org.freedesktop.DBus.Properties", "PropertiesChanged",
                on_properties_changed, NULL, s ) < 0 ||
        sd_bus_call_method_async( s->bus, NULL, "org.freedesktop.systemd1",
                "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager",
                "Subscribe", on_subscribed, s, "" ) < 0) {
        s->failed = 1;
        return 0;
    }
    query_state( s );
    return 0;
}

static void schedule_retry( awl_backlight_bus_t* s, uint64_t usec );

static void drop_bus( awl_backlight_bus_t* s ) {
    if (s->io) s->api->io_free( s->io );
    if (s->timeout) s->api->time_free( s->timeout );
    s->io = NULL;
    s->timeout = NULL;
    s->match = sd_bus_slot_unref( s->match );
    /* not sd_bus_flush_close_unref(): that blocks until the queue is out */
    if (s->bus) sd_bus_close( s->bus );
    s->bus = sd_bus_unref( s->bus );
    free( s->path );
    s->path = NULL;
    s->failed = 0;
}

static void bus_io( pa_mainloop_api* a, pa_io_event* e, int fd, pa_io_event_flags_t f, void* userdata ) {
    (void)a; (void)e; (void)fd; (void)f;
    bus_update( userdata );
}

static void bus_timeout( pa_mainloop_api* a, pa_time_event* e, const struct timeval* tv, void* userdata ) {
    (void)tv;
    awl_backlight_bus_t* s = userdata;
    a->time_free( e );
    s->timeout = NULL;
    bus_update( s );
}

static void connect_bus( awl_backlight_bus_t* s ) {
    if (sd_bus_open_user( &s->bus ) < 0 ||
        sd_bus_set_method_call_timeout( s->bus, CALL_TIMEOUT_USEC ) < 0 ||
        !(s->io = s->api->io_new( s->api, sd_bus_get_fd( s->bus ), PA_IO_EVENT_INPUT, bus_io, s )) ||
        sd_bus_call_method_async( s->bus, NULL, "org.freedesktop.systemd1",
                "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager",
                "LoadUnit", on_unit, s, "s", unit_name ) < 0) {
        drop_bus( s );
        schedule_retry( s, RETRY_USEC );
        return;
    }
    bus_update( s );
}

static void retry_callback( pa_mainloop_api* a, pa_time_event* e, const struct timeval* tv, void* userdata ) {
    (void)tv;
    awl_backlight_bus_t* s = userdata;
    a->time_free( e );
    s->retry = NULL;
    connect_bus( s );
}

static void schedule_retry( awl_backlight_bus_t* s, uint64_t usec ) {
    if (s->retry) return;
    struct timeval tv = in_usec( usec );
    s->retry = s->api->time_new( s->api, &tv, retry_callback, s );
}

/** Handles whatever the bus has, then waits for what it wants next. */
static void bus_update( awl_backlight_bus_t* s ) {
    int r;
    while ((r = sd_bus_process( s->bus, NULL )) > 0)
        ;
    if (r < 0 || s->failed) {
        /* connection lost or the unit can't be watched */
        drop_bus( s );
        schedule_retry( s, RETRY_USEC );
        return;
    }

    int events = sd_bus_get_events( s->bus );
    s->api->io_enable( s->io, (events & POLLIN ? PA_IO_EVENT_INPUT : 0) |
                              (events & POLLOUT ? PA_IO_EVENT_OUTPUT : 0) );

    uint64_t until;
    if (sd_bus_get_timeout( s->bus, &until ) < 0 || until == UINT64_MAX) {
        if (s->timeout) s->api->time_free( s->timeout );
        s->timeout = NULL;
        return;
    }
    /* until is on CLOCK_MONOTONIC */
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    uint64_t now = (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    struct timeval tv = in_usec( until > now ? until - now : 0 );
    if (s->timeout)
        s->api->time_restart( s->timeout, &tv );
    else
        s->timeout = s->api->time_new( s->api, &tv, bus_timeout, s );
}

awl_backlight_t* backlight_init( pa_mainloop_api* api ) {
    awl_backlight_t* b = calloc(1, sizeof *b);
    if (!b) return NULL;
    atomic_init( &b->enabled, 1 );
    if (!(b->bus = calloc(1, sizeof *b->bus))) {
        free( b );
        return NULL;
    }
    b->bus->b = b;
    b->bus->api = api;
    schedule_retry( b->bus, 0 );
    if (!b->bus->retry) {
        free( b->bus );
        free( b );
        return NULL;
    }
    return b;
}

void backlight_free( awl_backlight_t* b ) {
    if (!b) return;
    drop_bus( b->bus );
    if (b->bus->retry) b->bus->api->time_free( b->bus->retry );
    free( b->bus );
    free( b );
}
