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

#include "plugins/colors.h"

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

    struct awl_colors awl_colors;
} awl_plugin_data_t;

/* dwl's side of the boundary, set by awl_plugin_entry() */
extern const awl_host_t* awl_host;

/* NULL while the plugins aren't running */
awl_plugin_data_t* awl_plugin_get( void );

/* widgets.c */
void awl_widgets_create( Drwl* bar );

/* desktop_panel.c: the scanner thread, and the awl_plugin_api_t desktop_*
 * entries. stop() returns nonzero if the scanner had to be left running. */
void awl_desktop_start( void );
int awl_desktop_stop( void );
uint64_t awl_desktop_version( void );
void awl_desktop_measure( Drwl* drw, int avail_w, int avail_h, int r, float scale,
                          int* x, int* y, int* w, int* h );
void awl_desktop_draw( Drwl* drw, uint32_t* data, int stride, int w, int h, int r,
                       float scale );
int awl_desktop_click( int button );

/* awl_config.c */
const awl_config_t* awl_config( void );
