#include "date.h"
#include "bar.h"
#include "../awl_log.h"
#include "../awl_util.h"
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

#include "minimal_window.h"
#include "date_month.h"

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


struct awl_calendar_t {
    AWL_Window* w;
    month_state_t m;
};
static month_state_t* MST = NULL;

static void calendar_draw( AWL_SingleWindow* win, pixman_image_t* img ) {
    if (!MST) return;

    uint32_t width_want = TEXT_WIDTH(" Mo Tu We Th Fr Sa Su", -1, 20);
    if (!width_want) return;

    int x = 15,
        y = 0,
        dy = 35;
    char line[64] = {0};
    y += dy;

    pixman_image_t *fg = pixman_image_create_bits(PIXMAN_a8r8g8b8, win->width, win->height, NULL, win->width*4);
    pixman_image_t *bg = img;

    sprintf( line, "%-16s %4d", MST->monthname, MST->year );
    draw_text( line, x, y, fg, bg, &barcolors.fg_status, &barcolors.bg_status, win->width, dy, 0 );
    y += dy;

    sprintf( line, " Mo Tu We Th Fr Sa Su" );
    draw_text( line, x, y, fg, bg, &barcolors.fg_status, &barcolors.bg_status, win->width, dy, 0 );
    y += dy;

    int counter = 0;
    for (int i=0; i<MST->sday; ++i) {
        counter++;
        sprintf( line, "   " );
        x = draw_text( line, x, y, fg, bg, &barcolors.fg_status, &barcolors.bg_status, win->width, dy, 0 );
    }
    for (int d=1; d<=MST->ndays; d++) {
        sprintf( line, " %2d", d );
        int current = MST->year == MST->cyear && MST->month == MST->cmonth && d==MST->cday;
        x = draw_text( line, x, y, fg, bg, current ? &barcolors.fg_stats_mem : &barcolors.fg_status, &barcolors.bg_status, win->width, dy, 0 );
        counter++;
        if (counter == 7) {
            x = 15;
            y += dy;
            counter=0;
        }
    }

    // frame
    pixman_image_fill_boxes(PIXMAN_OP_OVER, bg, &barcolors.fg_stats_cpu, 1, &(pixman_box32_t){
                .x1 = 0, .x2 = win->width,
                .y1 = 0, .y2 = win->height,
            });
    pixman_image_fill_boxes(PIXMAN_OP_OVER, bg, &barcolors.bg_status, 1, &(pixman_box32_t){
                .x1 = 3, .x2 = win->width-6,
                .y1 = 3, .y2 = win->height-6,
            });

    // and fill into bg, free fg
    pixman_image_composite32(PIXMAN_OP_OVER, fg, NULL, bg, 0, 0, 0, 0, 0, 0, win->width, win->height);
    pixman_image_unref(fg);
}

awl_calendar_t* calendar_popup( void ) {
    awl_log_printf("init calendar popup window");
    awl_calendar_t* r = ecalloc(1, sizeof(awl_calendar_t));
    r->m = month_state_init();
    MST = &r->m;
    awl_minimal_window_props_t wp = awl_minimal_window_props_defaults;
    wp.only_current_output = 1;
    wp.width_want = 376;
    wp.height_want = 300;
    wp.layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
    wp.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM|ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    wp.name = "awl_cal_popup";
    wp.draw = calendar_draw;
    r->w = awl_minimal_window_setup( &wp );
    return r;
}

void calendar_hide( awl_calendar_t* cal ) {
    if (!cal) return;
    if (!awl_minimal_window_is_hidden( cal->w ))
        awl_minimal_window_hide( cal->w );
    cal->m = month_state_init();
}

void calendar_show( awl_calendar_t* cal ) {
    if (!cal) return;
    if (awl_minimal_window_is_hidden( cal->w ))
        awl_minimal_window_show( cal->w );
    awl_minimal_window_refresh( cal->w );
}

void calendar_next( awl_calendar_t* cal, int n ) {
    if (!cal) return;
    if (!awl_minimal_window_is_hidden( cal->w )) {
        month_state_next( &cal->m, n );
        awl_minimal_window_refresh( cal->w );
    }
}

void calendar_destroy( awl_calendar_t* cal ) {
    if (!cal) return;
    MST = NULL;
    awl_minimal_window_destroy( cal->w );
    free( cal );
}
