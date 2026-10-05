#pragma once

#include <stdatomic.h>

typedef struct awl_date_t {
    // local time of day in seconds (h*3600 + m*60 + s), -1 before the first
    // update; one value, so hours, minutes and seconds always match
    atomic_int secs;
} awl_date_t;

awl_date_t* date_init( void );
/* Re-reads the time; returns nonzero if it changed. */
int date_update( awl_date_t* d );
void date_free( awl_date_t* d );
