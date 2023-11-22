#pragma once

#include "../awl_arg.h"
#include "../awl_state.h"
#include "../awl_extension.h"

#include "ipaddr.h"
#include "temp.h"
#include "bat.h"
#include "pulsetest.h"
#include "date.h"

#include <unistd.h>

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
    void (*client_max)( const Arg* arg );
    void (*tagmon_f)( const Arg* arg );
    void (*bordertoggle)( const Arg* arg );
} awl_plugin_data_t;

awl_plugin_data_t* awl_plugin_data( void );

pid_t spawn_pid( char** arg );
pid_t spawn_pid_str_s( const char* cmd ); // argc < 16 && strlen(cmd) < 1024-16*8-2
pid_t spawn_pid_str( const char* cmd ); // long commands

extern awl_vtable_t AWL_VTABLE_SYM;

/* functions used from awl.h/awl.c
arrange
ecalloc
focusclient
focusmon
focusstack
focustop
killclient
moveresize
printstatus
resize
setlayout
setmfact
tag
tagmon
togglebar
togglefloating
togglefullscreen
toggletag
toggleview
view
*/
