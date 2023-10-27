#pragma once

#include "../awl_arg.h"

typedef struct Bar Bar;
void* awl_bar_run( void* addr );

extern bool awlb_run_display;

// initially hide all bars
extern bool hidden;
// initially draw all bars at the bottom
extern bool bottom;
// hide vacant tags
extern bool hide_vacant;
// vertical pixel padding above and below text
extern uint32_t vertical_padding;
// center title text
extern bool center_title;
// use title space as status text element
extern bool custom_title;
// scale
extern uint32_t buffer_scale;
// font
extern char *fontstr;
// tag names
extern char **tags_names;
extern uint32_t n_tags_names;

// set 16-bit colors for bar
// 8-bit color can be converted to 16-bit color by simply duplicating values e.g
// 0x55 -> 0x5555, 0xf1 -> 0xf1f1
extern pixman_color_t active_fg_color;
extern pixman_color_t active_bg_color;
extern pixman_color_t occupied_fg_color;
extern pixman_color_t occupied_bg_color;
extern pixman_color_t inactive_fg_color;
extern pixman_color_t inactive_bg_color;
extern pixman_color_t urgent_fg_color;
extern pixman_color_t urgent_bg_color;

inline pixman_color_t color_8bit_to_16bit( uint32_t c ) {
    uint8_t red = c & 0xFF000000,
            green = c & 0x00FF0000,
            blue = c & 0x0000FF00,
            alpha = c & 0x000000FF;
    pixman_color_t r = { .red = ((uint16_t)red) << 8 | red,
                         .green = ((uint16_t)green) << 8 | green,
                         .blue = ((uint16_t)blue) << 8 | blue,
                         .alpha = ((uint16_t)alpha) << 8 | alpha, };
    return r;
}

