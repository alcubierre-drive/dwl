#pragma once

#include "../awl_arg.h"

void cycle_tag( const Arg* arg );
void cycle_layout( const Arg* arg );

// awl.h functions with only their prototypes given here
void focusstack( const Arg* arg );
void view( const Arg* arg );
void toggleview( const Arg* arg );

// colors
static const int molokai_blue = 0x66d9efff;
static const int molokai_red = 0xf92672ff;
static const int molokai_green = 0xa6e22eff;
static const int molokai_orange = 0xfd971fff;
static const int molokai_purple = 0xae81ffff;
static const int molokai_gray = 0x232526ff;
static const int molokai_dark_gray = 0x1b1d1eff;
static const int molokai_light_gray = 0x455354ff;

