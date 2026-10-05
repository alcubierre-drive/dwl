/* g++ $(shell pkg-config libpulse --cflags --libs) pulsetest.c -o pulsetest */
#include "pthread_wrap.h"
/*#include "../awl_log.h"*/
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <pulse/pulseaudio.h>
#include "pulsetest.h"
#include "redraw.h"
/*#include "bar.h"*/
/*#include "init.h"*/

/* Tracks the default sink's volume/mute through PulseAudio subscriptions.
 * The thread runs a plain pa_mainloop that only wakes for server events
 * (sink changes, default sink changes). stop_pulse_thread() writes an
 * eventfd that is registered as an io event in that mainloop; its callback
 * calls pa_mainloop_quit() from the loop's own thread, so no PulseAudio
 * object is ever touched concurrently and the thread is never cancelled.
 * After the join everything is freed from the stopping thread. If the
 * server goes away (or isn't there yet), the last value is kept and a new
 * connection is attempted every RETRY_USEC; no timer runs while connected. */

#define RETRY_USEC (3 * PA_USEC_PER_SEC)

typedef struct PulseAudio {
    pa_mainloop* _mainloop;
    pa_mainloop_api* _mainloop_api;
    pa_context* _context;
    pa_io_event* _wake;
    pa_time_event* _retry;
    int wake_fd; // eventfd; written by stop_pulse_thread() to end the thread

    pulse_test_t* t;
} PulseAudio;

struct pulse_test_thread_t {
    PulseAudio PA;
    pthread_t me;
};

static void* pulse_thread_fun( void* arg );

static void wake_callback( pa_mainloop_api* a, pa_io_event* e, int fd, pa_io_event_flags_t f, void* userdata );
static void retry_callback( pa_mainloop_api* a, pa_time_event* e, const struct timeval* tv, void* userdata );
static void context_state_callback( pa_context* c, void* userdata );
static void subscribe_callback( pa_context* c, pa_subscription_event_type_t type, uint32_t idx, void* userdata );
static void sink_info_callback( pa_context* c, const pa_sink_info* i, int eol, void* userdata );
static void server_info_callback( pa_context* c, const pa_server_info* i, void* userdata );

static int PulseAudio_initialize( PulseAudio* p );
static void PulseAudio_connect( PulseAudio* p );
static void PulseAudio_drop_context( PulseAudio* p );
static void PulseAudio_schedule_retry( PulseAudio* p );
static void PulseAudio_destroy( PulseAudio* p );

pulse_test_t* start_pulse_thread( void ) {
    pulse_test_t* p = calloc(1, sizeof(pulse_test_t));
    if (!p) return NULL;
    p->h = calloc(1, sizeof(pulse_test_thread_t));
    if (!p->h) {
        free( p );
        return NULL;
    }
    p->h->PA.t = p;
    p->h->PA.wake_fd = -1;
    atomic_init( &p->value, 0 );
    atomic_init( &p->muted, 0 );
    sem_init( &p->sem, 0, 1 );

    if (!PulseAudio_initialize( &p->h->PA )) {
        PulseAudio_destroy( &p->h->PA );
        sem_destroy( &p->sem );
        free( p->h );
        free( p );
        return NULL;
    }
    int err = AWL_PTHREAD_CREATE( &p->h->me, NULL, pulse_thread_fun, p );
    if (err) {
        fprintf( stderr, "pulse: can't start the thread: %s\n", strerror( err ) );
        PulseAudio_destroy( &p->h->PA );
        sem_destroy( &p->sem );
        free( p->h );
        free( p );
        return NULL;
    }
    return p;
}

void stop_pulse_thread( pulse_test_t* p ) {
    if (p && p->h) {
        uint64_t one = 1;
        // only fails if the counter is full, i.e. the thread is due to wake anyway
        if (write( p->h->PA.wake_fd, &one, sizeof one ) < 0) {}
        pthread_join( p->h->me, NULL );
        PulseAudio_destroy( &p->h->PA );
        sem_destroy( &p->sem );
        free( p->h );
        free( p );
    }
}

// main()
static void* pulse_thread_fun( void* arg ) {
    pulse_test_t* p = (pulse_test_t*)arg;
    int ret = 0;
    pa_mainloop_run( p->h->PA._mainloop, &ret );
    p->ret = ret;
    return NULL;
}

static void wake_callback( pa_mainloop_api* a, pa_io_event* e, int fd, pa_io_event_flags_t f, void* userdata ) {
    (void)e; (void)fd; (void)f; (void)userdata;
    a->quit( a, 0 );
}

static void retry_callback( pa_mainloop_api* a, pa_time_event* e, const struct timeval* tv, void* userdata ) {
    (void)tv;
    PulseAudio* pa = (PulseAudio*)userdata;
    a->time_free( e );
    pa->_retry = NULL;
    PulseAudio_connect( pa );
}

static void context_state_callback( pa_context* c, void* userdata ) {
    PulseAudio* pa = (PulseAudio*)userdata;
    pa_operation* op = NULL;
    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_UNCONNECTED:
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
            break;
        case PA_CONTEXT_READY:
            /*P_awl_log_printf( "pulse connection established.." );*/
            pa_context_set_subscribe_callback( c, subscribe_callback, userdata );
            op = pa_context_subscribe( c, PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER, NULL, NULL );
            if (op) pa_operation_unref( op );
            op = pa_context_get_server_info( c, server_info_callback, userdata );
            if (op) pa_operation_unref( op );
            break;
        case PA_CONTEXT_TERMINATED:
        case PA_CONTEXT_FAILED:
        default:
            /*P_awl_err_printf( "pulse connection lost: %s", pa_strerror(pa_context_errno(c)) );*/
            // keep the last value; reconnect later
            PulseAudio_drop_context( pa );
            PulseAudio_schedule_retry( pa );
            break;
    }
}

static void subscribe_callback( pa_context* c, pa_subscription_event_type_t type, uint32_t idx, void* userdata ) {
    unsigned facility = type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    pa_operation* op = NULL;
    switch (facility) {
        case PA_SUBSCRIPTION_EVENT_SINK:
            op = pa_context_get_sink_info_by_index( c, idx, sink_info_callback, userdata );
            break;
        case PA_SUBSCRIPTION_EVENT_SERVER:
            // the default sink may have changed
            op = pa_context_get_server_info( c, server_info_callback, userdata );
            break;
        default:
            break;
    }
    if (op) pa_operation_unref( op );
}

static void sink_info_callback( pa_context* c, const pa_sink_info* i, int eol, void* userdata ) {
    (void)c;
    (void)eol;

    pulse_test_t* t = ((PulseAudio*)userdata)->t;
    if (i && i->name && t && !strcmp( i->name, t->name )) {
        float value = (float)pa_cvolume_avg(&(i->volume)) / (float)PA_VOLUME_NORM;
        int muted = i->mute;
        int changed = atomic_exchange( &t->value, value ) != value;
        changed |= atomic_exchange( &t->muted, muted ) != muted;
        if (changed) awl_redraw_request();
    }
}

static void server_info_callback( pa_context* c, const pa_server_info* i, void* userdata ) {
    PulseAudio* pa = userdata;
    // i is NULL on failure, default_sink_name is NULL when there is no sink
    const char* name = (i && i->default_sink_name) ? i->default_sink_name : "";
    /*P_awl_log_printf( "pulse sink name = %s", name );*/
    sem_wait( &pa->t->sem );
    int same = !strcmp( pa->t->name, name );
    if (!same) snprintf( pa->t->name, sizeof(pa->t->name), "%s", name );
    sem_post( &pa->t->sem );
    if (same || !name[0]) return;
    pa_operation* op = pa_context_get_sink_info_by_name( c, name, sink_info_callback, userdata );
    if (op) pa_operation_unref( op );
}

static int PulseAudio_initialize( PulseAudio* p ) {
    p->wake_fd = eventfd( 0, EFD_CLOEXEC | EFD_NONBLOCK );
    if (p->wake_fd < 0) {
        /*P_awl_err_printf( "pulse eventfd() failed." );*/
        return 0;
    }
    p->_mainloop = pa_mainloop_new();
    if (!p->_mainloop) {
        /*P_awl_err_printf( "pulse pa_mainloop_new() failed." );*/
        return 0;
    }
    p->_mainloop_api = pa_mainloop_get_api( p->_mainloop );
    p->_wake = p->_mainloop_api->io_new( p->_mainloop_api, p->wake_fd, PA_IO_EVENT_INPUT, wake_callback, p );
    if (!p->_wake) {
        /*P_awl_err_printf( "pulse io_new() failed." );*/
        return 0;
    }
    PulseAudio_connect( p );
    return 1;
}

/* Creates a context and starts connecting; on failure a retry is scheduled.
 * Called before the thread starts, then only from the loop thread. */
static void PulseAudio_connect( PulseAudio* p ) {
    p->_context = pa_context_new( p->_mainloop_api, "awl volume" );
    if (!p->_context) {
        /*P_awl_err_printf( "pulse pa_context_new() failed." );*/
        PulseAudio_schedule_retry( p );
        return;
    }
    pa_context_set_state_callback( p->_context, context_state_callback, p );
    if (pa_context_connect( p->_context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL ) < 0) {
        /*P_awl_err_printf( "pulse pa_context_connect() failed: %s", pa_strerror(pa_context_errno(p->_context)));*/
        PulseAudio_drop_context( p );
        PulseAudio_schedule_retry( p );
    }
}

static void PulseAudio_drop_context( PulseAudio* p ) {
    if (!p->_context) return;
    pa_context* c = p->_context;
    p->_context = NULL;
    pa_context_set_state_callback( c, NULL, NULL );
    pa_context_set_subscribe_callback( c, NULL, NULL );
    pa_context_disconnect( c );
    pa_context_unref( c );
    // force a fresh sink lookup after reconnecting
    sem_wait( &p->t->sem );
    p->t->name[0] = 0;
    sem_post( &p->t->sem );
}

static void PulseAudio_schedule_retry( PulseAudio* p ) {
    if (p->_retry) return;
    struct timeval tv;
    pa_timeval_add( pa_gettimeofday( &tv ), RETRY_USEC );
    p->_retry = p->_mainloop_api->time_new( p->_mainloop_api, &tv, retry_callback, p );
}

/* Only call when the loop thread is not running (joined or never started). */
static void PulseAudio_destroy( PulseAudio* p ) {
    PulseAudio_drop_context( p );

    if (p->_mainloop_api) {
        if (p->_retry) p->_mainloop_api->time_free( p->_retry );
        if (p->_wake) p->_mainloop_api->io_free( p->_wake );
    }
    p->_retry = NULL;
    p->_wake = NULL;

    if (p->_mainloop) {
        pa_mainloop_free( p->_mainloop );
        p->_mainloop = NULL;
        p->_mainloop_api = NULL;
    }

    if (p->wake_fd >= 0) {
        close( p->wake_fd );
        p->wake_fd = -1;
    }
}
