#pragma once
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <time.h>

#include <pixman-1/pixman.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-util.h>
#include "wlr-layer-shell-unstable-v1-protocol.h"

typedef struct AWL_Window AWL_Window;
typedef struct {
    struct wl_output *wl_output;
    struct wl_surface *wl_surface;
    struct zwlr_layer_surface_v1 *layer_surface;

    uint32_t registry_name;

    int configured;
    uint32_t width, height;
    uint32_t stride, bufsize;

    int hidden;
    int redraw;

    AWL_Window* parent;

    struct wl_list link;
} AWL_SingleWindow;

typedef struct awl_minimal_window_props_t {
    int buffer_scale;
    int hidden;
    int width_want; // 0 for full
    int height_want; // 0 for full
    uint32_t anchor; // ZWLR_LAYER_SURFACE_V1_ANCHOR_[BOTTOM|RIGHT|TOP|LEFT]
    uint32_t layer; // ZWLR_LAYER_SHELL_V1_LAYER_[BACKGROUND|BOTTOM|TOP|OVERLAY]
    const char* name;

    int only_current_output; // only one window on the current output
    double continuous_event_norm; // renormalization of pad scrolling

    void (*draw)( AWL_SingleWindow* win, pixman_image_t* foreground, pixman_image_t* background );
    void (*click)( AWL_SingleWindow* win, int button );
    void (*scroll)( AWL_SingleWindow* win, int amount );
} awl_minimal_window_props_t;

static const awl_minimal_window_props_t awl_minimal_window_props_defaults = {
    .buffer_scale = 2,
    .hidden = 1,
    .width_want = 0,
    .height_want = 0,
    .anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP|ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT,
    .layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
    .name = NULL,

    .only_current_output = 0,
    .continuous_event_norm = 10.0,

    .draw = NULL,
    .click = NULL,
    .scroll = NULL,
};

AWL_Window* awl_minimal_window_setup( const awl_minimal_window_props_t* props );
void awl_minimal_window_wait_ready( AWL_Window* w );

void awl_minimal_window_hide( AWL_Window* w );
void awl_minimal_window_show( AWL_Window* w );
void awl_minimal_window_refresh( AWL_Window* w );
int awl_minimal_window_is_hidden( AWL_Window* w );

void awl_minimal_window_resize( AWL_Window* W, int w, int h );

void awl_minimal_window_destroy( AWL_Window* w );

/*
int main() {
    AWL_Window* w = awl_minimal_window_setup( NULL );
    sleep(1);
    awl_minimal_window_show( w );
    sleep(1);
    awl_minimal_window_destroy( w );
    return 0;
}
*/
