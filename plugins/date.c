#include "date.h"
#include <time.h>
#include <stdlib.h>

awl_date_t* date_init( void ) {
    awl_date_t* d = calloc(1, sizeof(awl_date_t));
    atomic_init( &d->secs, -1 );
    return d;
}

int date_update( awl_date_t* d ) {
    time_t t;
    time(&t);
    struct tm lt;
    localtime_r(&t, &lt);
    int sec = lt.tm_sec > 59 ? 59 : lt.tm_sec; // leap second
    int secs = lt.tm_hour * 3600 + lt.tm_min * 60 + sec;
    return atomic_exchange( &d->secs, secs ) != secs;
}

void date_free( awl_date_t* d ) {
    free(d);
}
