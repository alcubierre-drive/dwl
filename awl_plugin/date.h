#pragma once

#include <pthread.h>

typedef struct awl_date_t {
    char s[128];
    int update_sec;
    pthread_t me;
    pthread_mutex_t mtx;
} awl_date_t;

awl_date_t* start_date_thread( int update_sec );
void stop_date_thread( awl_date_t* d );

typedef struct awl_calendar_t awl_calendar_t;

// TODO make this threadsafe (mutex + no static data)

awl_calendar_t* calendar_popup( void );
void calendar_destroy( awl_calendar_t* cal );

void calendar_hide( awl_calendar_t* cal );
void calendar_show( awl_calendar_t* cal );
void calendar_next( awl_calendar_t* cal, int n );
