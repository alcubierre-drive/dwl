#pragma once

#include "pthread_wrap.h"
#include <stdatomic.h>

typedef struct awl_backlight_t {
    atomic_int enabled;
    pthread_t me;
    int wake_fd; /* eventfd; written by stop_backlight_thread() to end the thread */
} awl_backlight_t;

awl_backlight_t* start_backlight_thread( void );
void stop_backlight_thread( awl_backlight_t* bat );
