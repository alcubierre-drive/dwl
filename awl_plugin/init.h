#pragma once

#include "../awl_arg.h"
#include "../awl_state.h"
#include "../awl_extension.h"

#include "ipaddr.h"
#include "temp.h"
#include "bat.h"
#include "pulsetest.h"
#include "date.h"

awl_config_t* awl_plugin_config( void );
awl_state_t* awl_plugin_state( void );

typedef struct awl_plugin_data_t {
    float refresh_sec;

    awl_ipaddr_t ip;
    awl_temperature_t temp;
    awl_battery_t bat;
    char* date;
    pulse_test_t* pulse;

    float cpu[128], mem[128], swp[128];
    int ncpu, nmem, nswp;
    int stats_dir;

    awl_calendar_t* cal;

    void (*cycle_tag)(const Arg* arg);
    void (*cycle_layout)(const Arg* arg);
    void (*movestack)( const Arg *arg );
    void (*client_hide)( const Arg* arg );
    void (*tagmon_f)( const Arg* arg );
    void (*bordertoggle)( const Arg* arg );
} awl_plugin_data_t;

awl_plugin_data_t* awl_plugin_data( void );

extern awl_vtable_t AWL_VTABLE_SYM;

/* functions used from awl.h/awl.c
arrange
ecalloc
focusclient
focusmon
focusstack
focustop
killclient
monocle
moveresize
printstatus
resize
setlayout
setmfact
spawn
tag
tagmon
tile
togglebar
togglefloating
togglefullscreen
toggletag
toggleview
view
zoom
*/
