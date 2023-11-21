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

pixman_color_t color_8bit_to_16bit( uint32_t c );
pixman_color_t alpha_blend_16( pixman_color_t B, pixman_color_t A );

#define COLOR_16BIT_QUICK( R, G, B, A ) { \
    .red = 0x##R##R, .green = 0x##G##G, .blue = 0x##B##B, .alpha = 0x##A##A, \
}

