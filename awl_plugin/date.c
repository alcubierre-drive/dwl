#include "date.h"
#include "bar.h"
#include "../awl_log.h"
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

static char date_string[128] = {0};
static int date_thread_run = 0;
static int date_thread_update_sec = 0;
static pthread_t* date_thread = NULL;

static void* date_thread_fun( void* arg ) {
    int update_sec = *(int*)arg;
    while (date_thread_run) {
        time_t t;
        time(&t);
        struct tm* lt = localtime(&t);
        strftime( date_string, 127, "%R", lt );
        /* strftime( date_string, 127, "%T", lt ); */
        /* awl_bar_refresh(); */
        sleep(update_sec);
    }
    return NULL;
}

char* start_date_thread( int update_sec ) {
    awl_log_printf( "starting time thread" );
    date_thread_update_sec = update_sec;
    date_thread_run = 1;
    date_thread = malloc(sizeof(pthread_t));
    pthread_create( date_thread, NULL, &date_thread_fun, &date_thread_update_sec );
    return date_string;
}

void stop_date_thread( void ) {
    date_thread_run = 0;
    date_thread_update_sec = 0;
    pthread_cancel(*date_thread);
    free(date_thread);
    date_thread = NULL;
}
