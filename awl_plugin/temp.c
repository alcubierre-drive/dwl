#include "temp.h"
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../awl_log.h"

struct temp_thread {
    int running;
    int sleep_sec;
    pthread_t me;
    FILE* f;
    awl_temperature_t* t;
};

static void temp_thread_cleanup( void* arg ) {
    struct temp_thread* T = arg;
    if (T) {
        T->running = 0;
        T->sleep_sec = 0;
        if (T->f) fclose(T->f);
    }
}

static void* temp_thread_run( void* arg ) {
    struct temp_thread* T = arg;
    if (!T) return NULL;
    pthread_cleanup_push( &temp_thread_cleanup, T );

    awl_temperature_t* temp = T->t;
    while (T->running) {
        temp->ready = 0;
        temp->ntemps = 0;
        for (int i=0; i<temp->f_ntemps; ++i) {
            if ((T->f = fopen(temp->f_files[i], "r"))) {
                long u = 0;
                if (fscanf(T->f, "%li", &u)) {
                    // save output
                    temp->temps[temp->ntemps] = (float)u/(float)1000.0;
                    temp->idx[temp->ntemps++] = i;
                }
                fclose(T->f);
                T->f = NULL;
            }
        }
        temp->ready = 1;
        sleep(T->sleep_sec);
    }

    pthread_cleanup_pop( 1 );
    return NULL;
}

static struct temp_thread T = {0};

void start_temp_thread( awl_temperature_t* temp, int update_sec ) {
    T.running = 1;
    T.sleep_sec = update_sec;
    T.t = temp;
    pthread_create( &T.me, NULL, temp_thread_run, &T );
}

void stop_temp_thread( void ) {
    if (T.running) {
        pthread_cancel( T.me );
        pthread_join( T.me, NULL );
    }
}
