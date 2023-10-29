#include "date.h"
#include <time.h>
#include <pthread.h>
#include <unistd.h>

static char date_string[128] = {0};
static int date_thread_run = 0;
static pthread_t date_thread;

static void* date_thread_fun( void* arg ) {
    int update_sec = *((int*)arg);
    while (date_thread_run) {
        time_t t;
        time(&t);
        struct tm* lt = localtime(&t);
        strftime( date_string, 127, "%R", lt );
        sleep(update_sec);
    }
    return NULL;
}

char* start_date_thread( int update_sec ) {
    date_thread_run = 1;
    pthread_create( &date_thread, NULL, date_thread_fun, &update_sec );
    return date_string;
}

void stop_date_thread( void ) {
    date_thread_run = 0;
    pthread_cancel(date_thread);
}
