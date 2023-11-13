#pragma once

typedef struct month_state_t {
    int cmonth,
        cday,
        cyear,
        month,
        year,
        wday,
        sday,
        lday;
    char monthname[32];
} month_state_t;

month_state_t month_state_init( void );
void month_state_next( month_state_t* st, int n );
