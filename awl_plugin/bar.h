#pragma once

#include <stdbool.h>
#include <pixman-1/pixman.h>
#include "colors.h"

typedef struct Bar Bar;
typedef struct awl_bar_handle_t awl_bar_handle_t;

awl_bar_handle_t* awl_bar_run( float refresh_sec );
void awl_bar_stop( awl_bar_handle_t* h );
void awl_bar_refresh( awl_bar_handle_t* h, int redraw );

typedef struct awlb_color_t {
    pixman_color_t bg_tags, bg_tags_occ, bg_tags_act, bg_tags_urg, fg_tags,
                   bg_lay, fg_lay,
                   bg_status, fg_status,
                   bg_win, bg_win_min, bg_win_act, bg_win_urg, fg_win,
                   bg_stats, fg_stats_cpu, fg_stats_mem, fg_stats_swp;
} awlb_color_t;

// TODO we're not doing window stuff yet, so those are accessible to the outside
// world
extern awlb_color_t barcolors;

// TODO because we're lazy with the draw_text function, we just expose it like
// so:
uint32_t draw_text(char *text, uint32_t x, uint32_t y,
      pixman_image_t *fg, pixman_image_t *bg,
      pixman_color_t *fg_color, pixman_color_t *bg_color,
      uint32_t max_x, uint32_t buf_height, uint32_t padding );
uint32_t draw_text_at(char *text, uint32_t x, uint32_t y,
        pixman_image_t *foreground, pixman_image_t *background,
        pixman_color_t *fg_color, pixman_color_t *bg_color, uint32_t max_x,
        uint32_t padding, uint32_t padding_add_y );

#define TEXT_WIDTH(text, maxwidth, padding) \
    draw_text(text, 0, 0, NULL, NULL, NULL, NULL, maxwidth, 0, padding)

