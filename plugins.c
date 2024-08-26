#include "dwl.h"
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
    return 0;
}

static void* wp_thread( void* data ) {
    while (1) {
        usleep(200);
        drawroot_update( &wpfunc );
    }
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
    p->pulse = start_pulse_thread();
    /*AWL_PTHREAD_CREATE( &p->wp_thread, NULL, wp_thread, NULL );*/
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
}

void awl_plugin_free( awl_plugin_data_t* p ) {
    awl_plugin_stop( p );
    free(p);
}

void awl_plugin_restart( awl_plugin_data_t* p ) {
    awl_plugin_stop(p);
    awl_plugin_start(p);
}
