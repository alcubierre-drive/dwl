#pragma once

#include "../awl_arg.h"
#include "pulsetest.h"

#include <pthread.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcft/fcft.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <pixman-1/pixman.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-util.h>

#define MMIN(a, b) ((a) < (b) ? (a) : (b))
#define MMAX(a, b) ((a) > (b) ? (a) : (b))

typedef struct Bar Bar;
void* awl_bar_run( void* arg );
void* awl_bar_refresh( void* arg );

extern char* awlb_date_txt;
extern pulse_test_t* awlb_pulse_info;
extern float *awlb_cpu_info, *awlb_mem_info, *awlb_swp_info;
extern int awlb_cpu_len, awlb_mem_len, awlb_swp_len;
extern int awlb_direction;

// TODO these should be accessible from the outside world
extern struct wl_list bar_list;
extern struct wl_list seat_list;

extern bool awlb_run_display;

// initially hide all bars
extern bool hidden;
// initially draw all bars at the bottom
extern bool bottom;
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

extern pixman_color_t bg_color_tags,
                      bg_color_tags_occ,
                      bg_color_tags_act,
                      bg_color_tags_urg,
                      fg_color_tags,

                      bg_color_lay,
                      fg_color_lay,

                      bg_color_status,
                      fg_color_status,

                      bg_color_win,
                      bg_color_win_min,
                      bg_color_win_act,
                      bg_color_win_urg,
                      fg_color_win,

                      bg_color_stats,
                      fg_color_stats_cpu,
                      fg_color_stats_mem,
                      fg_color_stats_swp;

static const pixman_color_t white = {.red = 0xFFFF, .green = 0xFFFF, .blue = 0xFFFF, .alpha = 0xFFFF};
static const pixman_color_t black = {.red = 0x0000, .green = 0x0000, .blue = 0x0000, .alpha = 0x0000};

pixman_color_t color_8bit_to_16bit( uint32_t c );
pixman_color_t alpha_blend_16( pixman_color_t B, pixman_color_t A );

#define COLOR_16BIT_QUICK( R, G, B, A ) { \
    .red = 0x##R##R, .green = 0x##G##G, .blue = 0x##B##B, .alpha = 0x##A##A, \
}

