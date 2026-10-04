#include "date.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>

awl_date_t* date_init( void ) {
    awl_date_t* d = calloc(1, sizeof(awl_date_t));
    sem_init( &d->sem, 0, 1 );
    return d;
}

int date_update( awl_date_t* d ) {
    time_t t;
    time(&t);
    struct tm lt;
    localtime_r(&t, &lt);
    char s[128] = {0};
    strftime( s, 127, "%R", &lt );
    /* strftime( s, 127, "%T", &lt ); */
    sem_wait( &d->sem );
    int changed = strcmp( s, d->s ) != 0 || d->sec != lt.tm_sec;
    if (changed) memcpy( d->s, s, sizeof s );
    d->sec = lt.tm_sec > 59 ? 59 : lt.tm_sec; // leap second
    sem_post( &d->sem );
    return changed;
}

void date_free( awl_date_t* d ) {
    sem_destroy( &d->sem );
    free(d);
}
