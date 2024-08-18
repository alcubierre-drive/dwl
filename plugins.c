#include "plugins.h"
#include "plugins/ipaddr.h"
#include "plugins/stats.h"
#include "plugins/temp.h"
#include "plugins/bat.h"
#include "plugins/date.h"
#include "plugins/pulsetest.h"

#include <stddef.h>
#include <unistd.h>

/*int usleep(useconds_t usec);*/

static void* drawbar_thread_fun( void* arg ) {
    // TODO
    awl_plugin_data_t* p = arg;
    if (!p) return NULL;
    while (1) {
        usleep( p->drawbar_sleep_secs * 1.e6 );
        void (*drawbars)(void) = (void(*)(void))atomic_load(&p->drawbars);
        if (drawbars) (*drawbars)();
    }
    return NULL;
}

awl_plugin_data_t* awl_plugin_init( void ) {
    awl_plugin_data_t* p = calloc(1,sizeof(awl_plugin_data_t));

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
    p->drawbar_sleep_secs = 0.2;
    AWL_PTHREAD_CREATE( &p->drawbar_thread, NULL, &drawbar_thread_fun, p );
    return p;
}

void awl_plugin_free( awl_plugin_data_t* p ) {
    stop_ip_thread(p->ip);
    stop_stats_thread(p->stats);
    stop_temp_thread(p->temp); free(p->temp);
    stop_bat_thread(p->bat);
    stop_date_thread(p->date);
    /*calendar_destroy(p->cal);*/
    stop_pulse_thread(p->pulse);

    if (!pthread_cancel(p->drawbar_thread)) pthread_join( p->drawbar_thread, NULL );

    free(p);
}
