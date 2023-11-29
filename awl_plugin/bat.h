#pragma once

#include "../awl_pthread.h"
#include <semaphore.h>

typedef struct awl_battery_t {
    _Atomic float charge;
    _Atomic int charging;
    // -1: invalid

    pthread_t me;
    sem_t sem;
    int update_sec;
} awl_battery_t;

awl_battery_t* start_bat_thread( int update_sec );
void stop_bat_thread( awl_battery_t* bat );
