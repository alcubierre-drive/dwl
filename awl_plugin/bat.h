#pragma once

#include <pthread.h>

typedef struct awl_battery_t {
    float charge;
    int charging;
    // -1: invalid

    pthread_t me;
    pthread_mutex_t mtx;
    int update_sec;
} awl_battery_t;

awl_battery_t* start_bat_thread( int update_sec );
void stop_bat_thread( awl_battery_t* bat );
