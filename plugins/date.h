#pragma once

#include <semaphore.h>

typedef struct awl_date_t {
    char s[128];
    int sec; // seconds into the current minute, 0-59
    sem_t sem;
} awl_date_t;

awl_date_t* date_init( void );
/* Re-reads the time; returns nonzero if the string or the second changed. */
int date_update( awl_date_t* d );
void date_free( awl_date_t* d );

typedef struct awl_calendar_t awl_calendar_t;

awl_calendar_t* calendar_popup( void );
void calendar_destroy( awl_calendar_t* cal );

void calendar_hide( awl_calendar_t* cal );
void calendar_show( awl_calendar_t* cal );
void calendar_next( awl_calendar_t* cal, int n );
