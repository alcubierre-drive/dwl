#pragma once

#include "../awl_arg.h"
#include "../awl_state.h"
#include "../awl_extension.h"

#include "ipaddr.h"
#include "temp.h"
#include "bat.h"

void cycle_tag( const Arg* arg );
void cycle_layout( const Arg* arg );

extern awl_vtable_t AWL_VTABLE_SYM;

// awl.h functions with only their prototypes given here
void focusstack( const Arg* arg );
void view( const Arg* arg );
void toggleview( const Arg* arg );

// colors
static const uint32_t molokai_blue = 0x66d9efff;
static const uint32_t molokai_red = 0xf92672ff;
static const uint32_t molokai_green = 0xa6e22eff;
static const uint32_t molokai_orange = 0xfd971fff;
static const uint32_t molokai_purple = 0xae81ffff;
static const uint32_t molokai_gray = 0x232526ff;
static const uint32_t molokai_dark_gray = 0x1b1d1eff;
static const uint32_t molokai_light_gray = 0x455354ff;

extern awl_ipaddr_t awl_ip;
extern awl_temperature_t awl_temp;
extern awl_battery_t awl_bat;

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
