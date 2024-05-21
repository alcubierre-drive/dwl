#pragma once

#include <stdatomic.h>

typedef struct pulse_test_thread_t pulse_test_thread_t;

typedef struct pulse_test_t {
    _Atomic float value;
    _Atomic int muted;
    _Atomic int headphones;

    int ret;

    pulse_test_thread_t* h;
} pulse_test_t;

pulse_test_t* start_pulse_thread( void );
void stop_pulse_thread( pulse_test_t* p );
