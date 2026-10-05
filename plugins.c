#include "dwl.h"
#include "plugins.h"
#include "plugins/ipaddr.h"
#include "plugins/stats.h"
#include "plugins/temp.h"
#include "plugins/bat.h"
#include "plugins/date.h"
#include "plugins/pulsetest.h"
#include "plugins/poller.h"
#include "plugins/redraw.h"

#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

static void awl_plugin_start( awl_plugin_data_t* p ) {
    p->awl_colors = awl_colors();

    p->ip = ip_init();
    p->stats = stats_init( 16, 16, 16 );

    p->temp = calloc(1,sizeof(awl_temperature_t));
    // CPU package sensor (cheap MSR read); the ACPI thermal zone goes through
    // the EC and costs ~2ms per read, so it is only the fallback
    if (temp_find_hwmon( "coretemp", "Package id", p->temp->f_files[p->temp->f_ntemps],
                         sizeof(p->temp->f_files[0]) )) {
        // idles around 50, throttles at TjMax (110)
        p->temp->f_t_max[p->temp->f_ntemps] = 100;
        p->temp->f_t_min[p->temp->f_ntemps] = 50;
    } else {
        strcpy( p->temp->f_files[p->temp->f_ntemps], "/sys/class/thermal/thermal_zone0/temp" );
        p->temp->f_t_max[p->temp->f_ntemps] = 90;
        p->temp->f_t_min[p->temp->f_ntemps] = 40;
    }
    strcpy( p->temp->f_labels[p->temp->f_ntemps++], "" );
    // here could go another thermal zone
    temp_init(p->temp);
    p->temp_color = &temp_color;

    p->bat = bat_init();
    p->date = date_init();
    p->poller = poller_start( p->stats, p->temp, p->date, p->bat, p->ip );
    poller_set_paused( p->poller, p->paused );
    p->pulse = start_pulse_thread();
    p->backlight = start_backlight_thread();
}

static void awl_plugin_stop( awl_plugin_data_t* p ) {
    poller_stop(p->poller);
    ip_free(p->ip);
    stats_free(p->stats);
    temp_fini(p->temp); free(p->temp);
    bat_free(p->bat);
    date_free(p->date);
    stop_pulse_thread(p->pulse);
    stop_backlight_thread(p->backlight);
}

/* everything below is the library's side of awl_plugin_abi.h */

const awl_host_t* awl_host = NULL;
static awl_plugin_data_t* plugin_data = NULL;

awl_plugin_data_t* awl_plugin_get( void ) {
    return plugin_data;
}

/* plugins/redraw.c lives in dwl, which owns the eventfd */
void awl_redraw_request( void ) {
    awl_host->redraw_request();
}

static void api_init( int paused ) {
    plugin_data = calloc(1,sizeof(awl_plugin_data_t));
    plugin_data->paused = paused;
    awl_plugin_start( plugin_data );
    awl_desktop_start();
}

static int api_fini( void ) {
    if (!plugin_data) return 0;
    int stuck = awl_desktop_stop();
    awl_plugin_stop( plugin_data );
    free( plugin_data );
    plugin_data = NULL;
    return stuck;
}

static void api_set_paused( int paused ) {
    if (!plugin_data) return;
    plugin_data->paused = paused;
    if (plugin_data->poller) poller_set_paused( plugin_data->poller, paused );
}

static const awl_plugin_api_t api = {
    .abi = AWL_PLUGIN_ABI,
    .init = api_init,
    .fini = api_fini,
    .set_paused = api_set_paused,
    .bar_widgets = awl_widgets_create,
    .desktop_version = awl_desktop_version,
    .desktop_measure = awl_desktop_measure,
    .desktop_draw = awl_desktop_draw,
    .desktop_click = awl_desktop_click,
    .config = awl_config,
};

__attribute__((visibility("default")))
const awl_plugin_api_t* awl_plugin_entry( const awl_host_t* host ) {
    if (!host || host->abi != AWL_PLUGIN_ABI ||
        host->sizeof_host != sizeof(awl_host_t) ||
        host->sizeof_drwl != sizeof(Drwl) ||
        host->sizeof_widget != sizeof(widget_t) ||
        host->sizeof_monitor != sizeof(Monitor) ||
        host->sizeof_client != sizeof(Client))
        return NULL;
    awl_host = host;
    return &api;
}
