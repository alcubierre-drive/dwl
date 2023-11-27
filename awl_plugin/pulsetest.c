/* g++ $(shell pkg-config libpulse --cflags --libs) pulsetest.c -o pulsetest */
#include <stdio.h>
#include <assert.h>
#include <pulse/pulseaudio.h>
#include <pthread.h>
#include "pulsetest.h"
#include "bar.h"
#include "init.h"
#include "../awl_log.h"

typedef struct PulseAudio {
    pa_mainloop* _mainloop;
    pa_mainloop_api* _mainloop_api;
    pa_context* _context;
    pa_signal_event* _signal;

    pulse_test_t* t;
} PulseAudio;

struct pulse_test_thread_t {
    PulseAudio PA;
    pthread_t me;
};

static void* pulse_thread_fun( void* arg );

static void exit_signal_callback(pa_mainloop_api *m, pa_signal_event *e, int sig, void *userdata);
static void context_state_callback(pa_context *c, void *userdata);
static void subscribe_callback(pa_context *c, pa_subscription_event_type_t type, uint32_t idx, void *userdata);
static void sink_info_callback(pa_context *c, const pa_sink_info *i, int eol, void *userdata);
static void server_info_callback(pa_context *c, const pa_server_info *i, void *userdata);
int PulseAudio_initialize( PulseAudio* p );

int PulseAudio_run( PulseAudio* p );
void PulseAudio_quit( PulseAudio* p, int ret );
void PulseAudio_destroy( PulseAudio* p );

pulse_test_t* start_pulse_thread( void ) {
    pulse_test_t* p = calloc(1, sizeof(pulse_test_t));
    p->h = calloc(1, sizeof(pulse_test_thread_t));
    p->h->PA.t = p;

    if (!PulseAudio_initialize( &p->h->PA )) {
        free( p->h );
        free( p );
        return NULL;
    }
    P_awl_log_printf( "create pulse_thread" );
    pthread_create( &p->h->me, NULL, pulse_thread_fun, p );
    return p;
}

void stop_pulse_thread( pulse_test_t* p ) {
    if (p && p->h) {
        PulseAudio_quit( &p->h->PA, 0 );
        pthread_join( p->h->me, NULL );
        PulseAudio_destroy( &p->h->PA );
        free( p->h );
        free( p );
    }
}

// main()
static void* pulse_thread_fun( void* arg ) {
    pulse_test_t* p = (pulse_test_t*)arg;
    int ret = PulseAudio_run( &p->h->PA );
    if (p) p->ret = ret;
    return NULL;
}

static void exit_signal_callback(pa_mainloop_api *m, pa_signal_event *e, int sig, void *userdata) {
    (void)m;
    (void)e;
    (void)sig;
    PulseAudio* pa = (PulseAudio*)userdata;
    if (pa)
        PulseAudio_quit(pa, 0);
}

static void context_state_callback(pa_context *c, void *userdata) {
    assert(c && userdata);
    PulseAudio* pa = (PulseAudio*)userdata;
    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
            break;
        case PA_CONTEXT_READY:
            P_awl_log_printf( "pulse connection established.." );
            pa_context_get_server_info(c, server_info_callback, userdata);
            pa_context_set_subscribe_callback(c, subscribe_callback, userdata);
            pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK, NULL, NULL);
            break;
        case PA_CONTEXT_TERMINATED:
            PulseAudio_quit(pa, 0);
            P_awl_log_printf( "pulse connection terminated.." );
            break;
        case PA_CONTEXT_FAILED:
        default:
            P_awl_err_printf( "pulse connection failure: %s", pa_strerror(pa_context_errno(c)) );
            PulseAudio_quit(pa, 1);
            break;
    }
}

static void subscribe_callback(pa_context *c, pa_subscription_event_type_t type, uint32_t idx, void *userdata) {
    unsigned facility = type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    //type &= PA_SUBSCRIPTION_EVENT_TYPE_MASK;
    pa_operation *op = NULL;
    switch (facility) {
        case PA_SUBSCRIPTION_EVENT_SINK:
            pa_context_get_sink_info_by_index(c, idx, sink_info_callback, userdata);
            break;
        default:
            assert(0); // Got event we aren't expecting.
            break;
    }
    if (op)
        pa_operation_unref(op);
}

static void sink_info_callback(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
    (void)c;
    (void)eol;

    pulse_test_t* t = ((PulseAudio*)userdata)->t;
    if (i && t) {
        t->value = (float)pa_cvolume_avg(&(i->volume)) / (float)PA_VOLUME_NORM;
        t->muted = i->mute;
    }
}

static void server_info_callback(pa_context *c, const pa_server_info *i, void *userdata) {
    P_awl_log_printf( "pulse sink name = %s", i->default_sink_name );
    pa_context_get_sink_info_by_name(c, i->default_sink_name, sink_info_callback, userdata);
}

int PulseAudio_initialize( PulseAudio* p ) {
    if (!p) {
        P_awl_err_printf( "pulse handle was NULL." );
        return 0;
    }
    p->_mainloop = pa_mainloop_new();
    if (!p->_mainloop) {
        P_awl_err_printf( "pulse pa_mainloop_new() failed." );
        return 0;
    }
    p->_mainloop_api = pa_mainloop_get_api(p->_mainloop);
    if (pa_signal_init(p->_mainloop_api) != 0) {
        P_awl_err_printf( "pulse pa_signal_init() failed." );
        return 0;
    }
    if (!(p->_signal = pa_signal_new(SIGINT, exit_signal_callback, p))) {;
        P_awl_err_printf( "pulse pa_signal_new() failed." );
        return 0;
    }
    if (!(p->_context = pa_context_new(p->_mainloop_api, "PulseAudio Test"))) {
        P_awl_err_printf( "pulse pa_context_new() failed." );
        return 0;
    }
    if (pa_context_connect(p->_context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) < 0) {
        P_awl_err_printf( "pulse pa_context_connect() failed: %s", pa_strerror(pa_context_errno(p->_context)));
        return 0;
    }
    pa_context_set_state_callback(p->_context, context_state_callback, p);

    return 1;
}

int PulseAudio_run( PulseAudio* p ) {
    int ret = 1;
    if (pa_mainloop_run(p->_mainloop, &ret) < 0) {
        P_awl_err_printf( "pulse pa_mainloop_run() failed.." );
        return ret;
    }
    return ret;
}

void PulseAudio_quit( PulseAudio* p, int ret ) {
    p->_mainloop_api->quit(p->_mainloop_api, ret);
}

void PulseAudio_destroy( PulseAudio* p ) {
    if (p->_context) {
        pa_context_unref(p->_context);
        p->_context = NULL;
    }

    if (p->_signal) {
        pa_signal_free(p->_signal);
        pa_signal_done();
        p->_signal = NULL;
    }

    if (p->_mainloop) {
        pa_mainloop_free(p->_mainloop);
        p->_mainloop = NULL;
        p->_mainloop_api = NULL;
    }
}

