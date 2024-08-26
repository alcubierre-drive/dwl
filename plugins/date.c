#include "date.h"
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include "pthread_wrap.h"

static void* date_thread_fun( void* arg ) {
    awl_date_t* d = (awl_date_t*)arg;
    while (1) {
        time_t t;
        time(&t);
        struct tm* lt = localtime(&t);
        sem_wait( &d->sem );
        strftime( d->s, 127, "%R", lt );
        /* strftime( date_string, 127, "%T", lt ); */
        sem_post( &d->sem );
        sleep(d->update_sec);
    }
    return NULL;
}

awl_date_t* start_date_thread( int update_sec ) {
    awl_date_t* d = calloc(1, sizeof(awl_date_t));
    d->update_sec = update_sec;
    /*P_awl_log_printf( "creating date_thread" );*/
    sem_init( &d->sem, 0, 1 );
    AWL_PTHREAD_CREATE( &d->me, NULL, &date_thread_fun, d );
    return d;
}

void stop_date_thread( awl_date_t* d ) {
    sem_wait( &d->sem );
    if (!pthread_cancel(d->me)) pthread_join( d->me, NULL );
    sem_destroy( &d->sem );
    free(d);
}

