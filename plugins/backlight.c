#include "backlight.h"
#include "redraw.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <time.h>
#include <sys/eventfd.h>
#include <systemd/sd-bus.h>

/* Tracks whether backlight-tooler.timer is active by listening to systemd's
 * PropertiesChanged signals on the user bus, instead of spawning
 * `systemctl --user is-active` every second. The thread sleeps in poll() on
 * the bus fd and an eventfd; stop_backlight_thread() writes the eventfd so
 * the thread exits on its own (no pthread_cancel in the middle of sd-bus
 * calls). If the bus is unavailable or the connection drops, the last known
 * state is kept and a reconnect is attempted every RETRY_MS. */

static const char unit_name[] = "backlight-tooler.timer";
#define RETRY_MS 30000

typedef struct {
    awl_backlight_t* b;
    int requery; /* ActiveState was invalidated without a value */
} bl_ctx_t;

static void set_from_state( awl_backlight_t* b, const char* state ) {
    /* same semantics as `systemctl is-active`'s first byte: only "inactive"
     * counts as disabled */
    int enabled = (state && state[0] == 'i') ? 0 : 1;
    if (atomic_exchange( &b->enabled, enabled ) != enabled)
        awl_redraw_request();
}

static int query_state( sd_bus* bus, const char* path, awl_backlight_t* b ) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    char* state = NULL;
    int r = sd_bus_get_property_string( bus, "org.freedesktop.systemd1", path,
            "org.freedesktop.systemd1.Unit", "ActiveState", &err, &state );
    sd_bus_error_free( &err );
    if (r < 0) return r;
    set_from_state( b, state );
    free( state );
    return 0;
}

/* PropertiesChanged(s interface, a{sv} changed, as invalidated) */
static int on_properties_changed( sd_bus_message* m, void* userdata, sd_bus_error* ret_error ) {
    (void)ret_error;
    bl_ctx_t* ctx = userdata;
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
            set_from_state( ctx->b, state );
        } else if (sd_bus_message_skip( m, "v" ) < 0) {
            return 0;
        }
        if (sd_bus_message_exit_container( m ) < 0) return 0;
    }
    if (sd_bus_message_exit_container( m ) < 0) return 0;

    char** invalidated = NULL;
    if (sd_bus_message_read_strv( m, &invalidated ) >= 0 && invalidated) {
        for (char** s = invalidated; *s; ++s)
            if (!strcmp( *s, "ActiveState" )) ctx->requery = 1;
        for (char** s = invalidated; *s; ++s) free( *s );
        free( invalidated );
    }
    return 0;
}

/* Returns 1 if a stop was requested, 0 if the connection was lost/failed. */
static int run_connected( awl_backlight_t* b ) {
    sd_bus* bus = NULL;
    sd_bus_slot* slot = NULL;
    sd_bus_message* reply = NULL;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    char* path = NULL;
    bl_ctx_t ctx = { .b = b };
    int stop = 0;

    if (sd_bus_open_user( &bus ) < 0) goto out;
    /* bound how long a stop can be delayed by a hanging setup call */
    sd_bus_set_method_call_timeout( bus, 2000000 );

    const char* p = NULL;
    if (sd_bus_call_method( bus, "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                "org.freedesktop.systemd1.Manager", "LoadUnit", &err, &reply, "s", unit_name ) < 0)
        goto out;
    if (sd_bus_message_read( reply, "o", &p ) < 0 || !(path = strdup( p )))
        goto out;

    /* install the match before reading the initial state so no change can
     * slip through in between */
    if (sd_bus_match_signal( bus, &slot, "org.freedesktop.systemd1", path,
                "org.freedesktop.DBus.Properties", "PropertiesChanged",
                on_properties_changed, &ctx ) < 0)
        goto out;

    /* systemd only emits unit PropertiesChanged signals while at least one
     * client is subscribed; the subscription ends with our connection */
    sd_bus_error_free( &err );
    if (sd_bus_call_method( bus, "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                "org.freedesktop.systemd1.Manager", "Subscribe", &err, NULL, "" ) < 0)
        goto out;

    if (query_state( bus, path, b ) < 0) goto out;

    while (1) {
        int r;
        while ((r = sd_bus_process( bus, NULL )) > 0)
            ;
        if (r < 0) break; /* connection lost */
        if (ctx.requery) {
            ctx.requery = 0;
            if (query_state( bus, path, b ) < 0) break;
            continue;
        }

        struct pollfd pfd[2] = {
            { .fd = sd_bus_get_fd( bus ), .events = sd_bus_get_events( bus ) },
            { .fd = b->wake_fd, .events = POLLIN },
        };
        uint64_t usec = UINT64_MAX;
        int timeout = -1;
        if (sd_bus_get_timeout( bus, &usec ) >= 0 && usec != UINT64_MAX) {
            struct timespec ts;
            clock_gettime( CLOCK_MONOTONIC, &ts );
            uint64_t now = (uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000;
            timeout = usec > now ? (int)((usec - now + 999) / 1000) : 0;
        }
        if (poll( pfd, 2, timeout ) < 0) continue; /* EINTR */
        if (pfd[1].revents) { stop = 1; break; }
    }

out:
    sd_bus_error_free( &err );
    sd_bus_message_unref( reply );
    sd_bus_slot_unref( slot );
    sd_bus_flush_close_unref( bus );
    free( path );
    return stop;
}

static void* backlight( void* arg ) {
    if (!arg) return NULL;
    awl_backlight_t* b = arg;
    while (!run_connected( b )) {
        /* bus/unit unavailable or connection dropped: keep the last known
         * state and retry rarely, waking up early on stop */
        struct pollfd pfd = { .fd = b->wake_fd, .events = POLLIN };
        if (poll( &pfd, 1, RETRY_MS ) > 0) break;
    }
    return NULL;
}

awl_backlight_t* start_backlight_thread( void ) {
    awl_backlight_t* b = calloc(1, sizeof *b);
    if (!b) return NULL;
    atomic_init( &b->enabled, 1 );
    b->wake_fd = eventfd( 0, EFD_CLOEXEC | EFD_NONBLOCK );
    if (b->wake_fd < 0) {
        perror( "backlight: eventfd" );
        free(b);
        return NULL;
    }
    int err = AWL_PTHREAD_CREATE( &b->me, NULL, backlight, b );
    if (err) {
        fprintf( stderr, "backlight: can't start the thread: %s\n", strerror( err ) );
        close( b->wake_fd );
        free(b);
        return NULL;
    }
    return b;
}

void stop_backlight_thread( awl_backlight_t* b ) {
    if (!b) return;
    uint64_t one = 1;
    // only fails if the counter is full, i.e. the thread is due to wake anyway
    if (write( b->wake_fd, &one, sizeof one ) < 0) {}
    pthread_join( b->me, NULL );
    close( b->wake_fd );
    free(b);
}
