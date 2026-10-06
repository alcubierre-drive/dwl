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
#include <stdio.h>
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
    // one thread for all of them, see plugins/poller.h
    p->poller = poller_new( p->stats, p->temp, p->date, p->bat, p->ip );
    if (!p->poller) return;
    p->pulse = pulse_init( poller_api( p->poller ) );
    p->backlight = backlight_init( poller_api( p->poller ) );
    poller_set_paused( p->poller, p->paused );
    if (poller_start( p->poller )) {
        pulse_free( p->pulse );
        backlight_free( p->backlight );
        poller_free( p->poller );
        p->pulse = NULL;
        p->backlight = NULL;
        p->poller = NULL;
    }
}

static void awl_plugin_stop( awl_plugin_data_t* p ) {
    poller_stop(p->poller);
    pulse_free(p->pulse);
    backlight_free(p->backlight);
    poller_free(p->poller);
    ip_free(p->ip);
    stats_free(p->stats);
    free(p->temp);
    bat_free(p->bat);
    date_free(p->date);
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
    const WallpaperConfig* wp = awl_config()->wallpaper;
    awl_wallpaper_start( wp->dir );
}

static int api_fini( void ) {
    if (!plugin_data) return 0;
    int stuck = awl_desktop_stop();
    stuck |= awl_wallpaper_stop();
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

static void api_wallpaper( WallpaperMode mode ) {
    switch (mode) {
    case WallpaperNext: awl_wallpaper_step( +1 ); break;
    case WallpaperPrev: awl_wallpaper_step( -1 ); break;
    case WallpaperRand: awl_wallpaper_random(); break;
    }
}

/* the wallpaper timer's mode changed (wallpapermode): which one it goes
 * from and to, counted from 1 */
void awl_notify( const char* title, const char* body ) {
    awl_host->actions->spawn( &(Arg){ .v = (const char*[]){
            "notify-send", "-a", "dwl", title, body, NULL } } );
}

static void api_wallpaper_mode( WallpaperMode mode ) {
    static const char* names[] = {
        [WallpaperRand] = "rand", [WallpaperNext] = "next", [WallpaperPrev] = "prev" };
    const char* off = awl_config()->wallpaper->interval ? "" : " (timer off)";
    char body[96];
    int cur, n, rand_next;
    if (awl_wallpaper_position( &cur, &n, &rand_next )) {
        int to = mode == WallpaperNext ? (cur + 1) % n
               : mode == WallpaperPrev ? (cur + n - 1) % n
               : rand_next;
        snprintf( body, sizeof(body), "%s: %d → %d of %d%s", names[mode], cur + 1, to + 1, n, off );
    } else {
        snprintf( body, sizeof(body), "%s%s", names[mode], off );
    }
    awl_notify( "Wallpaper timer", body );
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
    .wallpaper = api_wallpaper,
    .wallpaper_take = awl_wallpaper_take,
    .wallpaper_mode = api_wallpaper_mode,
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
