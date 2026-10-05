#pragma once

/* Internal to libawlplugins.so; dwl itself only sees awl_plugin_abi.h. */

#include "awl_plugin_abi.h"

#include "plugins/ipaddr.h"
#include "plugins/stats.h"
#include "plugins/temp.h"
#include "plugins/bat.h"
#include "plugins/backlight.h"
#include "plugins/date.h"
#include "plugins/pulsetest.h"
#include "plugins/poller.h"

#include "plugins/sem_time.h"
#include "plugins/colors.h"

typedef struct awl_calendar_t awl_calendar_t;
typedef struct awl_wallpaper_data_t awl_wallpaper_data_t;

typedef struct awl_plugin_data_t {
    float refresh_sec;

    awl_ipaddr_t* ip;
    awl_stats_t* stats;
    awl_temperature_t* temp;
    uint32_t (*temp_color)( float T, float min, float max );
    awl_battery_t* bat;
    awl_date_t* date;
    pulse_test_t* pulse;
    awl_backlight_t* backlight;
    awl_poller_t* poller;
    int paused;

    /*awl_wallpaper_data_t* wp;*/

    struct awl_colors awl_colors;

    pthread_t wp_thread;
} awl_plugin_data_t;

/* dwl's side of the boundary, set by awl_plugin_entry() */
extern const awl_host_t* awl_host;

/* NULL while the plugins aren't running */
awl_plugin_data_t* awl_plugin_get( void );

/* widgets.c */
void awl_widgets_create( Drwl* bar );
