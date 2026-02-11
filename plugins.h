#pragma once

#include "plugins/ipaddr.h"
#include "plugins/stats.h"
#include "plugins/temp.h"
#include "plugins/bat.h"
#include "plugins/backlight.h"
#include "plugins/date.h"
#include "plugins/pulsetest.h"

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

    /*awl_wallpaper_data_t* wp;*/

    struct awl_colors awl_colors;

    pthread_t wp_thread;
} awl_plugin_data_t;

awl_plugin_data_t* awl_plugin_init( void );
void awl_plugin_free( awl_plugin_data_t* p );

void awl_plugin_restart( awl_plugin_data_t* p );
