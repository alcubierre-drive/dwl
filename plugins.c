#include "plugins.h"
#include "plugins/ipaddr.h"
#include "plugins/stats.h"
#include "plugins/temp.h"
#include "plugins/bat.h"
#include "plugins/date.h"
#include "plugins/pulsetest.h"

#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#ifdef USLEEP_NOT_DEFINED
int usleep(useconds_t usec);
#endif

static void* drawbar_thread_fun( void* arg ) {
    awl_plugin_data_t* p = arg;
    if (!p) return NULL;
    usleep( 0.5 * 1e6 );
    while (atomic_load(&p->drawbar_run)) {
        usleep( p->drawbar_sleep_secs * 1.e6 );
        void (*drawbars)(void) = (void(*)(void))atomic_load(&p->drawbars);
        /*if (drawbars) (*drawbars)();*/ // TODO IMPLEMENT THIS WITH A TIMER IN THE EVENT LOOP
    }
    return NULL;
}

static int wpfunc( pixman_image_t* pix ) {
    int w = pixman_image_get_width(pix),
        h = pixman_image_get_height(pix);
    int x=0;
    pixman_color_t colors[] = {black, {.red=0x1111, .green=0x1111, .blue=0x1111, .alpha=0xFFFF}};
    int cidx=0;
    while (x < h && x < w) {
        pixman_box32_t box = {.x1=x, .y1=x, .x2=w-x, .y2=h-x};
        pixman_image_fill_boxes(PIXMAN_OP_SRC, pix, &colors[cidx], 1, &box);
        cidx++;
        cidx %= 2;
        x += 10;
    }
    return 1;
}

static void* drawroot_setter_thread_fun( void* arg ) {
    awl_plugin_data_t* p = arg;
    if (!p) return NULL;

    typedef int (*wallpaper_func_t)( pixman_image_t* pix );
    typedef void (*drawroot_setter_t)( wallpaper_func_t func );
    drawroot_setter_t drawroot_setter  = NULL;

    while (!(drawroot_setter = (drawroot_setter_t)atomic_load(&p->drawroot_setter))) {
        usleep( 0.2e6 );
    }

    (*drawroot_setter)( &wpfunc );
    return NULL;
}

static void awl_plugin_start( awl_plugin_data_t* p ) {
    p->awl_colors = awl_colors();

    p->ip = start_ip_thread(1);
    p->stats = start_stats_thread( 16, 16, 16, 1 );

    p->temp = calloc(1,sizeof(awl_temperature_t));
        // setup of first thermal zone
        strcpy( p->temp->f_files[p->temp->f_ntemps], "/sys/class/thermal/thermal_zone7/temp" );
        strcpy( p->temp->f_labels[p->temp->f_ntemps], "" );
        p->temp->f_t_max[p->temp->f_ntemps] = 80;
        p->temp->f_t_min[p->temp->f_ntemps++] = 40;
        // here could go another thermal zone
        start_temp_thread(p->temp, 1);
        p->temp_color = &temp_color;

    p->bat = start_bat_thread(1);
    p->date = start_date_thread(1);
    /*p->cal = calendar_popup();*/ // TODO
    p->pulse = start_pulse_thread();

    /*awl_wallpaper_data_t* wp;*/ // TODO

    atomic_init( &p->drawbars, 0 );
    atomic_init( &p->drawbar_run, 1 );
    p->drawbar_sleep_secs = 0.2;
    AWL_PTHREAD_CREATE( &p->drawbar_thread, NULL, &drawbar_thread_fun, p );

    atomic_init( &p->drawroot_setter, 0 );
    AWL_PTHREAD_CREATE( &p->drawroot_setter_thread, NULL, &drawroot_setter_thread_fun, p );
}

awl_plugin_data_t* awl_plugin_init( void ) {
    awl_plugin_data_t* p = calloc(1,sizeof(awl_plugin_data_t));
    awl_plugin_start( p );
    return p;
}

static void awl_plugin_stop( awl_plugin_data_t* p ) {
    stop_ip_thread(p->ip);
    stop_stats_thread(p->stats);
    stop_temp_thread(p->temp); free(p->temp);
    stop_bat_thread(p->bat);
    stop_date_thread(p->date);
    /*calendar_destroy(p->cal);*/
    stop_pulse_thread(p->pulse);

    atomic_store( &p->drawbar_run, 0 );
    pthread_join( p->drawbar_thread, NULL );
    pthread_join( p->drawroot_setter_thread, NULL );
}

void awl_plugin_free( awl_plugin_data_t* p ) {
    awl_plugin_stop( p );
    free(p);
}

void awl_plugin_restart( awl_plugin_data_t* p ) {
    uint64_t drw_bars = atomic_load( &p->drawbars ),
             drw_rootset = atomic_load( &p->drawroot_setter );
    awl_plugin_stop(p);
    awl_plugin_start(p);
    atomic_store( &p->drawbars, drw_bars );
    atomic_store( &p->drawroot_setter, drw_rootset );
}
