#pragma once

#include "../awl_arg.h"
#include "../awl_state.h"
#include "../awl_extension.h"

#include "ipaddr.h"
#include "stats.h"

#include "temp.h"
#include "bat.h"
#include "pulsetest.h"
#include "date.h"

#include <unistd.h>

awl_config_t* awl_plugin_config( void );
awl_state_t* awl_plugin_state( void );

typedef struct awl_plugin_data_t {
    float refresh_sec;

    awl_ipaddr_t* ip;
    awl_stats_t* stats;
    awl_temperature_t* temp;
    awl_battery_t* bat;
    awl_date_t* date;
    pulse_test_t* pulse;

    awl_calendar_t* cal;

    void (*cycle_tag)( const Arg* );
    void (*cycle_layout)( const Arg* );
    void (*movestack)( const Arg* );
    void (*client_hide)( const Arg* );
    void (*client_max)( const Arg* );
    void (*tagmon_f)( const Arg* );
    void (*bordertoggle)( const Arg* );
    void (*focusstack)( const Arg* );
    void (*toggleview)( const Arg* );
    void (*view)( const Arg* );
} awl_plugin_data_t;

awl_plugin_data_t* awl_plugin_data( void );

pid_t spawn_pid( char** arg );
pid_t spawn_pid_str_s( const char* cmd ); // argc < 16 && strlen(cmd) < 1024-16*8-2
pid_t spawn_pid_str( const char* cmd ); // long commands

extern awl_vtable_t AWL_VTABLE_SYM;
