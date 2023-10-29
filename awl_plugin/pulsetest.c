/* g++ $(shell pkg-config libpulse --cflags --libs) pulsetest.c -o pulsetest */
#include <stdio.h>
#include <assert.h>
#include <pulse/pulseaudio.h>
#include <pthread.h>
#include "pulsetest.h"

typedef struct PulseAudio {
    pa_mainloop* _mainloop;
    pa_mainloop_api* _mainloop_api;
    pa_context* _context;
    pa_signal_event* _signal;
} PulseAudio;

static PulseAudio PA = {0};
static pulse_test_t pulse_result = {0};
static pthread_t* pulse_thread = NULL;
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
    if (!PulseAudio_initialize( &PA )) return NULL;
    pulse_thread = malloc(sizeof(pthread_t));
    pthread_create( pulse_thread, NULL, pulse_thread_fun, &pulse_result );
    return &pulse_result;
}

void stop_pulse_thread( void ) {
    if (pulse_thread) {
        PulseAudio_quit( &PA, 0 );
        pthread_join( *pulse_thread, NULL );
        PulseAudio_destroy( &PA );
        free(pulse_thread);
        pulse_thread = NULL;
    }
}

// main()
static void* pulse_thread_fun( void* arg ) {
    int ret = PulseAudio_run( &PA );
    pulse_test_t* t = (pulse_test_t*)arg;
    if (!t) t->ret = ret;
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
            fprintf(stderr, "PulseAudio connection established.\n");
            pa_context_get_server_info(c, server_info_callback, userdata);
            pa_context_set_subscribe_callback(c, subscribe_callback, userdata);
            pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK, NULL, NULL);
            break;
        case PA_CONTEXT_TERMINATED:
            PulseAudio_quit(pa, 0);
            fprintf(stderr, "PulseAudio connection terminated.\n");
            break;
        case PA_CONTEXT_FAILED:
        default:
            fprintf(stderr, "Connection failure: %s\n", pa_strerror(pa_context_errno(c)));
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
    (void)userdata;
    if (i) {
        float volume = (float)pa_cvolume_avg(&(i->volume)) / (float)PA_VOLUME_NORM;
        pulse_result.value = volume;
        pulse_result.muted = i->mute;
    }
}

static void server_info_callback(pa_context *c, const pa_server_info *i, void *userdata) {
    fprintf(stderr, "default sink name = %s\n", i->default_sink_name);
    pa_context_get_sink_info_by_name(c, i->default_sink_name, sink_info_callback, userdata);
}

int PulseAudio_initialize( PulseAudio* p ) {
    if (!p) {
        fprintf(stderr, "handle was NULL\n");
        return 0;
    }
    p->_mainloop = pa_mainloop_new();
    if (!p->_mainloop) {
        fprintf(stderr, "pa_mainloop_new() failed.\n");
        return 0;
    }
    p->_mainloop_api = pa_mainloop_get_api(p->_mainloop);
    if (pa_signal_init(p->_mainloop_api) != 0) {
        fprintf(stderr, "pa_signal_init() failed\n");
        return 0;
    }
    if (!(p->_signal = pa_signal_new(SIGINT, exit_signal_callback, p))) {;
        fprintf(stderr, "pa_signal_new() failed\n");
        return 0;
    }
    if (!(p->_context = pa_context_new(p->_mainloop_api, "PulseAudio Test"))) {
        fprintf(stderr, "pa_context_new() failed\n");
        return 0;
    }
    if (pa_context_connect(p->_context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) < 0) {
        fprintf(stderr, "pa_context_connect() failed: %s\n", pa_strerror(pa_context_errno(p->_context)));
        return 0;
    }
    pa_context_set_state_callback(p->_context, context_state_callback, p->_mainloop_api);

    return 1;
}

int PulseAudio_run( PulseAudio* p ) {
    int ret = 1;
    if (pa_mainloop_run(p->_mainloop, &ret) < 0) {
        fprintf(stderr, "pa_mainloop_run() failed.\n");
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

