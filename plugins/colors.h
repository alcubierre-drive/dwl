#pragma once

#include <stdint.h>
#include <pixman-1/pixman.h>

// colors
static const uint32_t molokai_blue = 0x66d9efff;
static const uint32_t molokai_red = 0xf92672ff;
static const uint32_t molokai_green = 0xa6e22eff;
static const uint32_t molokai_orange = 0xfd971fff;
static const uint32_t molokai_purple = 0xae81ffff;
static const uint32_t molokai_gray = 0x232526ff;
static const uint32_t molokai_dark_gray = 0x1b1d1eff;
static const uint32_t molokai_light_gray = 0x455354ff;

static const pixman_color_t white = {.red = 0xFFFF, .green = 0xFFFF, .blue = 0xFFFF, .alpha = 0xFFFF};
static const pixman_color_t black = {.red = 0x0000, .green = 0x0000, .blue = 0x0000, .alpha = 0xFFFF};

static inline uint32_t color_16bit_to_8bit( pixman_color_t c ) {
    return (c.red >> 8) << 24 |
           (c.green >> 8) << 16 |
           (c.blue >> 8) << 8 |
           (c.alpha >> 8) << 0;
}

static inline pixman_color_t color_8bit_to_16bit( uint32_t c ) {
    uint8_t red = (c & 0xFF000000) >> 24,
            green = (c & 0x00FF0000) >> 16,
            blue = (c & 0x0000FF00) >> 8,
            alpha = c & 0x000000FF;
    pixman_color_t r = { .red = ((uint16_t)red) << 8 | red,
                         .green = ((uint16_t)green) << 8 | green,
                         .blue = ((uint16_t)blue) << 8 | blue,
                         .alpha = ((uint16_t)alpha) << 8 | alpha, };
    return r;
}

pixman_color_t alpha_blend_16( pixman_color_t B, pixman_color_t A );
pixman_color_t mean_color_16( pixman_color_t A, pixman_color_t B, float wA );

struct awl_colors {
    pixman_color_t bg_tags, bg_tags_occ, bg_tags_act, bg_tags_urg, fg_tags,
                   bg_lay, fg_lay, // also bg/fg for al standard widgets
                   bg_status, fg_status,
                   bg_win, bg_win_min, bg_win_act, bg_win_urg, fg_win,
                   bg_stats, fg_stats_cpu, fg_stats_mem, fg_stats_swp;
};
struct awl_colors awl_colors( void );

#define COLOR_16BIT_QUICK( R, G, B, A ) { \
    .red = 0x##R##R, .green = 0x##G##G, .blue = 0x##B##B, .alpha = 0x##A##A, \
}

