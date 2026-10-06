#pragma once

#include <stdatomic.h>
#include <pulse/mainloop-api.h>

typedef struct PulseAudio PulseAudio;

/** The default sink's volume. */
typedef struct pulse_test_t {
    _Atomic float value;
    atomic_int muted;

    /** loop thread only */
    PulseAudio* pa;
} pulse_test_t;

/** Tracks the default sink on the loop behind api (poller.h); NULL on
 * failure. */
pulse_test_t* pulse_init( pa_mainloop_api* api );
/** Only once the loop thread has stopped. No-op for NULL. */
void pulse_free( pulse_test_t* p );
