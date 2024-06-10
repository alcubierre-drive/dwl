#pragma once

#include <stdatomic.h>
#include <semaphore.h>

typedef struct pulse_test_thread_t pulse_test_thread_t;

typedef struct pulse_test_t {
    _Atomic float value;
    _Atomic int muted;
    _Atomic int headphones;

    int ret;
    sem_t sem;
    char name[256];
    char ports[16][256];
    int port;
    int n_ports;

    pulse_test_thread_t* h;
} pulse_test_t;

pulse_test_t* start_pulse_thread( void );
void stop_pulse_thread( pulse_test_t* p );

void pulse_thread_toggle_headphones( pulse_test_t* p );
