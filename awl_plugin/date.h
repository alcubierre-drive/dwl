#pragma once

#include "../awl_pthread.h"
#include <semaphore.h>

typedef struct awl_date_t {
    char s[128];
    int update_sec;
    pthread_t me;
    sem_t sem;
} awl_date_t;

awl_date_t* start_date_thread( int update_sec );
void stop_date_thread( awl_date_t* d );

typedef struct awl_calendar_t awl_calendar_t;

awl_calendar_t* calendar_popup( void );
void calendar_destroy( awl_calendar_t* cal );

void calendar_hide( awl_calendar_t* cal );
void calendar_show( awl_calendar_t* cal );
void calendar_next( awl_calendar_t* cal, int n );
