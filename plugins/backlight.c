#include "backlight.h"
#include "redraw.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <poll.h>
#include <time.h>
#include <sys/time.h>
#include <systemd/sd-bus.h>

/** Tracks the backlight-tooler daemon (``backlight-tooler -D``) through its
 * D-Bus interface: whether it owns ``org.BacklightTooler`` on the user bus,
 * whether its automatic updates are on (``Interval`` > 0), and the last
 * screen and webcam brightness it saw. All of it is followed by signals,
 * NameOwnerChanged and the daemon's PropertiesChanged, so nothing is
 * polled. Runs on the plugins' shared loop (poller.h): the bus
 * fd and its timeout are event sources there, and every call is
 * asynchronous, so a slow bus never holds up the other plugins. Nothing ever
 * starts the daemon. Everything but the atomics is only touched on the loop
 * thread, or before it starts and after it has stopped; the first
 * connection is made from the loop too. If the bus is unavailable or the
 * connection drops, the last known state is kept and a reconnect is
 * attempted every RETRY_USEC. */

#define BUS_NAME  "org.BacklightTooler"
#define BUS_PATH  "/org/BacklightTooler"
#define BUS_IFACE "org.BacklightTooler"
#define RETRY_USEC (30 * 1000000ull)
/** how long a call may go unanswered before it counts as failed */
#define CALL_TIMEOUT_USEC (2 * 1000000ull)

struct awl_backlight_bus_t {
    awl_backlight_t* b;
    pa_mainloop_api* api;
    sd_bus* bus;
    sd_bus_slot* owner_match;
    sd_bus_slot* props_match;
    pa_io_event* io;
    pa_time_event* timeout; /* the bus's next timeout, NULL if none */
    pa_time_event* retry;   /* the next connection attempt, NULL if none */
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

static int set_int( atomic_int* a, int v ) {
    return atomic_exchange( a, v ) != v;
}

/** brightness in [0,1] (or -1 for none) as per mille, -1 if unknown */
static int to_permille( double v ) {
    return (v >= 0 && v <= 1) ? (int)(v * 1000 + 0.5) : -1;
}

/** the daemon went away: nothing is known anymore */
static void set_gone( awl_backlight_t* b ) {
    int changed = set_int( &b->running, 0 );
    changed |= set_int( &b->enabled, 0 );
    changed |= set_int( &b->screen, -1 );
    changed |= set_int( &b->webcam, -1 );
    if (changed) awl_redraw_request();
}

/** a method reply: whether it is a proper one; marks the connection as
 * broken otherwise */
static int reply_ok( sd_bus_message* m, awl_backlight_bus_t* s ) {
    if (!sd_bus_message_is_method_error( m, NULL )) return 1;
    s->failed = 1;
    return 0;
}

/** Reads an a{sv} of the daemon's properties, keeps those we track and
 * requests a redraw if any of them changed. `running`: 1 if the daemon is
 * known to run now (a GetAll reply), 0 to leave it as is. */
static void read_properties( awl_backlight_t* b, sd_bus_message* m, int running ) {
    int changed = running ? set_int( &b->running, 1 ) : 0;
    if (sd_bus_message_enter_container( m, 'a', "{sv}" ) < 0) goto out;
    while (sd_bus_message_enter_container( m, 'e', "sv" ) > 0) {
        const char* key = NULL;
        if (sd_bus_message_read( m, "s", &key ) < 0) goto out;
        int r;
        if (!strcmp( key, "Interval" )) {
            uint64_t interval;
            if ((r = sd_bus_message_read( m, "v", "t", &interval )) >= 0)
                changed |= set_int( &b->enabled, interval != 0 );
        } else if (!strcmp( key, "ScreenBrightness" ) || !strcmp( key, "WebcamValue" )) {
            double v;
            if ((r = sd_bus_message_read( m, "v", "d", &v )) >= 0)
                changed |= set_int( key[0] == 'S' ? &b->screen : &b->webcam, to_permille( v ) );
        } else {
            r = sd_bus_message_skip( m, "v" );
        }
        if (r < 0 || sd_bus_message_exit_container( m ) < 0) goto out;
    }
    sd_bus_message_exit_container( m );
out:
    if (changed) awl_redraw_request();
}

/** Properties.GetAll(BUS_IFACE) -> a{sv}. An error means the daemon went
 * away or hangs; the former is told by NameOwnerChanged, which follows, and
 * the latter is no reason to drop the connection. */
static int on_properties( sd_bus_message* m, void* userdata, sd_bus_error* ret_error ) {
    (void)ret_error;
    awl_backlight_bus_t* s = userdata;
    if (!sd_bus_message_is_method_error( m, NULL ))
        read_properties( s->b, m, 1 );
    return 0;
}

static void query_properties( awl_backlight_bus_t* s ) {
    /* built by hand to turn off auto-start: the bar never starts the daemon */
    sd_bus_message* m = NULL;
    if (sd_bus_message_new_method_call( s->bus, &m, BUS_NAME, BUS_PATH,
                "org.freedesktop.DBus.Properties", "GetAll" ) < 0 ||
        sd_bus_message_set_auto_start( m, 0 ) < 0 ||
        sd_bus_message_append( m, "s", BUS_IFACE ) < 0 ||
        sd_bus_call_async( s->bus, NULL, m, on_properties, s, 0 ) < 0)
        s->failed = 1;
    sd_bus_message_unref( m );
}

/** PropertiesChanged(s interface, a{sv} changed, as invalidated), only the
 * daemon's interface (arg0 in the match) */
static int on_properties_changed( sd_bus_message* m, void* userdata, sd_bus_error* ret_error ) {
    (void)ret_error;
    awl_backlight_bus_t* s = userdata;
    if (sd_bus_message_skip( m, "s" ) < 0) return 0;
    /* not `running` yet: a signal can come before the GetAll reply that
     * follows the daemon's start, and alone would flash a running daemon
     * with its updates off */
    read_properties( s->b, m, 0 );
    /* read_properties() stops early on a malformed message; then this fails */
    char** invalidated = NULL;
    if (sd_bus_message_read_strv( m, &invalidated ) >= 0 && invalidated) {
        int requery = invalidated[0] != NULL;
        for (char** i = invalidated; *i; ++i) free( *i );
        free( invalidated );
        if (requery) query_properties( s );
    }
    return 0;
}

/** NameOwnerChanged(s name, s old_owner, s new_owner), only for BUS_NAME
 * (arg0 in the match) */
static int on_owner_changed( sd_bus_message* m, void* userdata, sd_bus_error* ret_error ) {
    (void)ret_error;
    awl_backlight_bus_t* s = userdata;
    const char *name = NULL, *old_owner = NULL, *new_owner = NULL;
    if (sd_bus_message_read( m, "sss", &name, &old_owner, &new_owner ) < 0) return 0;
    if (new_owner[0])
        query_properties( s );
    else
        set_gone( s->b );
    return 0;
}

/** GetNameOwner(s) -> s; NameHasNoOwner if the daemon isn't running */
static int on_owner( sd_bus_message* m, void* userdata, sd_bus_error* ret_error ) {
    (void)ret_error;
    awl_backlight_bus_t* s = userdata;
    const sd_bus_error* e = sd_bus_message_get_error( m );
    if (e && sd_bus_error_has_name( e, SD_BUS_ERROR_NAME_HAS_NO_OWNER ))
        set_gone( s->b );
    else if (reply_ok( m, s ))
        query_properties( s );
    return 0;
}

static int on_subscribed( sd_bus_message* m, void* userdata, sd_bus_error* ret_error ) {
    (void)ret_error;
    reply_ok( m, userdata );
    return 0;
}

static void schedule_retry( awl_backlight_bus_t* s, uint64_t usec );

static void drop_bus( awl_backlight_bus_t* s ) {
    if (s->io) s->api->io_free( s->io );
    if (s->timeout) s->api->time_free( s->timeout );
    s->io = NULL;
    s->timeout = NULL;
    s->owner_match = sd_bus_slot_unref( s->owner_match );
    s->props_match = sd_bus_slot_unref( s->props_match );
    /* not sd_bus_flush_close_unref(): that blocks until the queue is out */
    if (s->bus) sd_bus_close( s->bus );
    s->bus = sd_bus_unref( s->bus );
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
    /* The bus handles these in order, so both matches are in place before
     * the owner is asked for, and no change can slip through in between. */
    if (sd_bus_open_user( &s->bus ) < 0 ||
        sd_bus_set_method_call_timeout( s->bus, CALL_TIMEOUT_USEC ) < 0 ||
        !(s->io = s->api->io_new( s->api, sd_bus_get_fd( s->bus ), PA_IO_EVENT_INPUT, bus_io, s )) ||
        sd_bus_add_match_async( s->bus, &s->owner_match,
                "type='signal',sender='org.freedesktop.DBus',path='/org/freedesktop/DBus',"
                "interface='org.freedesktop.DBus',member='NameOwnerChanged',arg0='" BUS_NAME "'",
                on_owner_changed, on_subscribed, s ) < 0 ||
        sd_bus_add_match_async( s->bus, &s->props_match,
                "type='signal',sender='" BUS_NAME "',path='" BUS_PATH "',"
                "interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',"
                "arg0='" BUS_IFACE "'",
                on_properties_changed, on_subscribed, s ) < 0 ||
        sd_bus_call_method_async( s->bus, NULL, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                "org.freedesktop.DBus", "GetNameOwner", on_owner, s, "s", BUS_NAME ) < 0) {
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
        /* connection lost or the daemon can't be watched */
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
    atomic_init( &b->running, 0 );
    atomic_init( &b->enabled, 0 );
    atomic_init( &b->screen, -1 );
    atomic_init( &b->webcam, -1 );
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
