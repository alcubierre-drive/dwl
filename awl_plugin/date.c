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

static void calendar_draw( AWL_SingleWindow* win, pixman_image_t* fg, pixman_image_t* bg ) {
    awl_log_printf("redrawing calendar window");
    (void)bg;
    // TODO need to externalize draw_text in order to use it here... or some
    // funky acrobatics
    if (!MST) return;
    pixman_color_t white = {.red = 0xaaaa, .green = 0xaaaa, .blue = 0xaaaa, .alpha = 0xaaaa};
    pixman_image_fill_boxes(PIXMAN_OP_OVER, fg, &white, 1, &(pixman_box32_t){
                .x1 = 0, .x2 = win->width,
                .y1 = 0, .y2 = win->height,
            });
}

awl_calendar_t* calendar_popup( void ) {
    awl_log_printf("init calendar popup window");
    awl_calendar_t* r = ecalloc(1, sizeof(awl_calendar_t));
    r->m = month_state_init();
    MST = &r->m;
    awl_minimal_window_props_t wp = awl_minimal_window_props_defaults;
    wp.only_current_output = 1;
    wp.width_want = 240;
    wp.height_want = 240;
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
