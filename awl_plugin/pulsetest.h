#pragma once

typedef struct {
    float value;
    int muted;

    int ret;
} pulse_test_t;

pulse_test_t* start_pulse_thread( void );
void stop_pulse_thread( void );
