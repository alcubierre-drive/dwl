#pragma once

char* start_date_thread( int update_sec );
void stop_date_thread( void );

typedef struct awl_calendar_t awl_calendar_t;

awl_calendar_t* calendar_popup( void );
void calendar_destroy( awl_calendar_t* cal );

void calendar_hide( awl_calendar_t* cal );
void calendar_show( awl_calendar_t* cal );
void calendar_next( awl_calendar_t* cal, int n );
