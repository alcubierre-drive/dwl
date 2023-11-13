#include <time.h>
#include <langinfo.h>
#include <stdio.h>
#include <string.h>

static int ndays( int month, int year ) {
    switch (month) {
        // Cases for 31 Days
        case 1:
        case 3:
        case 5:
        case 7:
        case 8:
        case 10:
        case 12:
            return 31; break;
        // Cases for 30 Days
        case 4:
        case 6:
        case 9:
        case 11:
            return 30; break;
        // Case for 28/29 Days
        case 2:
            if ((year%400==0) || ((year%100!=0)&&(year%4==0)))
                return 29;
            else
                return 28;
            break;
        // invalid
        default:
            return -1; break;
    }
}

static int get_first_day_of_month( int cday /* 1..31 */, int wday /* 0..6 */ ) {
    int mrest = cday,
        mstart = wday;
    while (--mrest) mstart--;
    while (mstart < 0) mstart += 7;
    mstart %= 7;
    return mstart;
}

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

static int month_idx_to_macro( int idx ) {
    switch(idx) {
        case 1: return MON_1;
        case 2: return MON_2;
        case 3: return MON_3;
        case 4: return MON_4;
        case 5: return MON_5;
        case 6: return MON_6;
        case 7: return MON_7;
        case 8: return MON_8;
        case 9: return MON_9;
        case 10: return MON_10;
        case 11: return MON_11;
        case 12: return MON_12;
        default: return MON_1;
    }
}

static char* month_idx_to_name( int idx ) {
    return nl_langinfo( month_idx_to_macro(idx) );
}

month_state_t month_state_init( void ) {
    month_state_t St;
    month_state_t* st = &St;
    time_t t = {0};
    time(&t);
    struct tm tm = {0};
    localtime_r(&t, &tm);

    st->cmonth = st->month = tm.tm_mon + 1;
    st->cyear = st->year = tm.tm_year + 1900;
    st->cday = tm.tm_mday;

    st->wday = (tm.tm_wday + 6) % 7;
    st->sday = get_first_day_of_month( st->cday, st->wday );
    st->lday = (st->sday + ndays(st->month, st->year)-1) % 7;
    strcpy( st->monthname, month_idx_to_name(st->month) );
    return St;
}

void month_state_next( month_state_t* st, int n ) {
    if (!n) return;

    if (n>0) {
        st->month++;
    }
    if (n<0) {
        st->month--;
    }

    if (st->month < 1) {
        st->year--;
        st->month += 12;
    }
    if (st->month > 12) {
        st->year++;
        st->month -= 12;
    }

    if (n>0) {
        st->sday = (st->lday + 1)%7;
        st->lday = (st->sday + ndays(st->month, st->year)-1)%7;
    }
    if (n<0) {
        st->lday = (st->sday + 6)%7;
        st->sday = (st->lday + 35 - ndays(st->month, st->year)+1)%7;
    }

    strcpy( st->monthname, month_idx_to_name(st->month) );
}

/* static void month_state_print( const month_state_t* st ) { */
/*     printf( "%-16s %4d\n", st->monthname, st->year ); */
/*     printf( " Mo Tu We Th Fr Sa Su\n" ); */
/*     int counter = 0; */
/*     for (int i=0; i<st->sday; ++i) { */
/*         counter++; */
/*         printf("   "); */
/*     } */
/*     for (int d=1; d<=ndays(st->month, st->year); d++) { */
/*         if (st->year == st->cyear && st->month == st->cmonth && d==st->cday) printf("\e[0;31m"); */
/*         printf(" %2d", d); */
/*         if (st->year == st->cyear && st->month == st->cmonth && d==st->cday) printf("\e[0m"); */
/*         counter++; */
/*         if (counter == 7) { */
/*             printf("\n"); */
/*             counter=0; */
/*         } */
/*     } */
/*     if (counter != 0) */
/*         printf("\n"); */
/* } */

