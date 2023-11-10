#pragma once

#include <stdbool.h>
#include <pixman-1/pixman.h>
#include "colors.h"

typedef struct Bar Bar;
void* awl_bar_run( void* arg );
void* awl_bar_refresh( void* arg );

typedef struct awlb_color_t {
    pixman_color_t bg_tags, bg_tags_occ, bg_tags_act, bg_tags_urg, fg_tags,
                   bg_lay, fg_lay,
                   bg_status, fg_status,
                   bg_win, bg_win_min, bg_win_act, bg_win_urg, fg_win,
                   bg_stats, fg_stats_cpu, fg_stats_mem, fg_stats_swp;
} awlb_color_t;

// we're not doing window stuff yet, so those are accessible to the outside
// world
extern awlb_color_t barcolors;
extern bool awlb_run_display;
extern struct wl_list bar_list;
extern struct wl_list seat_list;
