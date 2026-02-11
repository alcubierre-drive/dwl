#pragma once

#include "pthread_wrap.h"
#include <semaphore.h>

typedef struct awl_backlight_t {
    _Atomic int enabled;
    pthread_t me;
    sem_t sem;
    float update_sec;
} awl_backlight_t;

awl_backlight_t* start_backlight_thread( float update_sec );
void stop_backlight_thread( awl_backlight_t* bat );
