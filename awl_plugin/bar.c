#define _GNU_SOURCE
#include "bar.h"

#include <pthread.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcft/fcft.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-util.h>

#include <utlist.h>

#include "../awl.h"
#include "../awl_arg.h"
#include "../awl_title.h"
#include "../awl_log.h"

#include "init.h"
#include "pulsetest.h"

struct awl_bar_handle_t {
    int redraw_fd;
    float refresh_sec;

    _Atomic int running;

    pthread_t bar;
    sem_t bar_init_sem;
    pthread_t refresh;
};

#define MIN(A,B) ((A)<(B)?(A):(B))
#define MAX(A,B) ((A)>(B)?(A):(B))

enum pointer_event_mask {
    POINTER_EVENT_ENTER = 1 << 0,
    POINTER_EVENT_LEAVE = 1 << 1,
    POINTER_EVENT_MOTION = 1 << 2,
    POINTER_EVENT_BUTTON = 1 << 3,
    POINTER_EVENT_AXIS = 1 << 4,
    POINTER_EVENT_AXIS_SOURCE = 1 << 5,
    POINTER_EVENT_AXIS_STOP = 1 << 6,
    POINTER_EVENT_AXIS_DISCRETE = 1 << 7,
};

struct pointer_event {
    uint32_t event_mask;
    wl_fixed_t surface_x, surface_y;
    uint32_t button, state;
    uint32_t time;
    uint32_t serial;
    struct {
            bool valid;
            wl_fixed_t value;
            int32_t discrete;
    } axes[2];
    uint32_t axis_source;
};

static const uint32_t vertical_padding = 2;
static const uint32_t buffer_scale = 2;
static const char fontstr[] = "monospace:size=10";
static char default_layout_name[] = "[T]";
static const char* tags_names[] = { "1", "2", "3", "4", "5", "6", "7", "✉ 8", "✉ 9" };
static const uint32_t n_tags_names = 9;

static pixman_box32_t* widget_boxes = NULL;

awlb_color_t barcolors = {
    .bg_tags = COLOR_16BIT_QUICK( 22, 22, 22, FF ),
    .bg_tags_occ = COLOR_16BIT_QUICK( 22, 22, 55, FF ),
    .bg_tags_act = COLOR_16BIT_QUICK( 22, 33, 77, FF ),
    .bg_tags_urg = COLOR_16BIT_QUICK( 77, 33, 22, FF ),
    .fg_tags = COLOR_16BIT_QUICK( EE, EE, FF, FF ),

    .bg_lay = COLOR_16BIT_QUICK( 11, 11, 11, FF ),
    .fg_lay = COLOR_16BIT_QUICK( FF, EE, EE, FF ),

    .bg_status = COLOR_16BIT_QUICK( 11, 11, 11, FF ),
    .fg_status = COLOR_16BIT_QUICK( FF, EE, EE, FF ),

    .bg_win = COLOR_16BIT_QUICK( 22, 22, 22, FF ),
    .bg_win_min = COLOR_16BIT_QUICK( 11, 11, 11, FF ),
    .bg_win_act = COLOR_16BIT_QUICK( 22, 22, 55, FF ),
    .bg_win_urg = COLOR_16BIT_QUICK( 55, 22, 22, FF ),
    .fg_win = COLOR_16BIT_QUICK( EE, EE, FF, FF ),

    .bg_stats = COLOR_16BIT_QUICK( 11, 11, 11, FF ),
    .fg_stats_cpu = COLOR_16BIT_QUICK( 55, 11, 11, FF ),
    .fg_stats_mem = COLOR_16BIT_QUICK( 11, 55, 11, FF ),
    .fg_stats_swp = COLOR_16BIT_QUICK( 11, 11, 55, FF ),
};

#include "utf8.h"
#include "xdg-shell-protocol.h"
#include "xdg-output-unstable-v1-protocol.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"
#include "awl-ipc-unstable-v2-protocol.h"

#define TEXT_MAX 2048

typedef struct {
    pixman_color_t color;
    bool bg;
    char *start;
} Color;

typedef struct {
    char text[TEXT_MAX];
} CustomText;

typedef struct widget_t widget_t;

struct widget_t {
    uint32_t width;
    uint32_t (*draw)(Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background);
    void (*callback_view)(Bar* bar, int32_t x_rel);
    void (*callback_click)(Bar* bar, uint32_t x_rel, int button);
    void (*callback_scroll)(Bar* bar, uint32_t x_rel, int amount);
};

struct Bar {
    struct wl_output *wl_output;
    struct wl_surface *wl_surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct zxdg_output_v1 *xdg_output;
    struct zdwl_ipc_output_v2 *dwl_wm_output;

    uint32_t registry_name;
    char *xdg_output_name;

    bool configured;
    uint32_t width, height;
    uint32_t textpadding;
    uint32_t stride, bufsize;

    uint32_t mtags, ctags, urg, sel;
    awl_title_t* window_titles;
    awl_title_t* window_list;
    int n_window_titles;
    int n_window_list;

    awl_title_t* cpy_window_list;
    int cpy_n_window_list;

    char* layout;
    uint32_t layout_idx;

    bool hidden;
    bool redraw;

    widget_t widgets_left[32];
    int n_widgets_left;
    widget_t widgets_right[32];
    int n_widgets_right;
    widget_t center_widget;
    uint32_t center_widget_space;
    uint32_t center_widget_start;
    int has_center_widget;

    sem_t draw_sem;
    awl_bar_handle_t* handle;

    Bar *prev, *next;
};

typedef struct Seat Seat;
struct Seat {
    struct wl_seat *wl_seat;
    struct wl_pointer *wl_pointer;
    uint32_t registry_name;
    struct pointer_event pointer_event;

    Bar *bar;
    uint32_t pointer_x, pointer_y;
    uint32_t pointer_button;

    Seat *prev, *next;
};

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct zxdg_output_manager_v1 *output_manager;

static struct zdwl_ipc_manager_v2 *dwl_wm;
static struct wl_cursor_image *cursor_image;
static struct wl_surface *cursor_surface;

static Bar* bar_list = NULL;
static Seat* seat_list = NULL;

static char layouts[128][16] = {0};
static int n_layouts = 0;
static char tags[128][16] = {0};
static int n_tags = 0;

#define STATIC_ARRAY_APPEND_STR( name, item ) do { \
    if ( n_##name >= 128 ) break; \
    strncpy( name[ n_##name ++ ], item, 15 ); \
} while (0);

static struct fcft_font *font;
static sem_t font_sem = {0};
static int font_sem_nusers = 32;

#define CHECKFONT( expr, fail ) \
    if (sem_trywait( &font_sem ) == -1) return fail; \
    if (!font) { \
        sem_post( &font_sem ); \
        return fail ; \
    } else { \
        expr; \
        sem_post( &font_sem ); \
    }

static uint32_t height, textpadding;

static void window_array_to_list( Bar* bar, int slot ) {
    if (slot == 0) {
        bar->window_list = NULL;
        bar->n_window_list = 0;
        if (!bar->window_titles) return;

        for (int i=0; i<bar->n_window_titles; ++i) {
            awl_title_t *s, *t=bar->window_titles+i;
            HASH_FIND_INT(bar->window_list, &t->id, s);
            if (!s)
                HASH_ADD_INT(bar->window_list, id, t);
        }
        bar->n_window_list = HASH_COUNT(bar->window_list);
    } else {
        bar->cpy_window_list = NULL;
        bar->cpy_n_window_list = 0;
        if (!bar->window_titles) return;

        for (int i=0; i<bar->n_window_titles; ++i) {
            awl_title_t *s, *t=bar->window_titles+i;
            HASH_FIND_INT(bar->cpy_window_list, &t->id, s);
            if (!s)
                HASH_ADD_INT(bar->cpy_window_list, id, t);
        }
        bar->cpy_n_window_list = HASH_COUNT(bar->cpy_window_list);
    }
}

static void wl_buffer_release(void *data, struct wl_buffer *wl_buffer) {
    (void)data;
    /* Sent by the compositor when it's no longer using this buffer */
    wl_buffer_destroy(wl_buffer);
}

static const struct wl_buffer_listener wl_buffer_listener = {
    .release = wl_buffer_release,
};

/* Shared memory support function adapted from [wayland-book] */
static int allocate_shm_file(size_t size) {
    int fd = memfd_create("surface", MFD_CLOEXEC);
    if (fd == -1)
        return -1;
    int ret;
    do {
        ret = ftruncate(fd, size);
    } while (ret == -1 && errno == EINTR);
    if (ret == -1) {
        close(fd);
        return -1;
    }
    return fd;
}

uint32_t draw_text_at(char *text, uint32_t x, uint32_t y,
        pixman_image_t *foreground, pixman_image_t *background,
        pixman_color_t *fg_color, pixman_color_t *bg_color, uint32_t max_x,
        uint32_t padding, uint32_t padding_add_y ) {

    if (!font) return x;

    if (!text || !*text || !max_x)
        return x;

    uint32_t ix = x, nx;

    if ((nx = x + padding) + padding >= max_x)
        return x;
    x = nx;

    bool draw_fg = foreground && fg_color;
    bool draw_bg = background && bg_color;

    pixman_image_t *fg_fill = NULL;
    pixman_color_t *cur_bg_color = NULL;
    if (draw_fg)
        fg_fill = pixman_image_create_solid_fill(fg_color);
    if (draw_bg)
        cur_bg_color = bg_color;

    uint32_t ymin = y, ymax = y;
    uint32_t codepoint, state = UTF8_ACCEPT, last_cp = 0;
    // find the y extents
    for (char *p = text; *p; p++) {
        if (utf8decode(&state, &codepoint, *p))
            continue;
        const struct fcft_glyph *glyph = NULL;
        CHECKFONT(glyph = fcft_rasterize_char_utf32(font, codepoint, FCFT_SUBPIXEL_NONE), x);
        if (!glyph)
            continue;
        ymin = MIN(ymin, y-glyph->y);
        ymax = MAX(ymax, y-glyph->y+glyph->height);
    }

    ymin = MIN(ymin, y-padding-padding_add_y);
    ymax = MAX(ymax, y+padding);

    for (char *p = text; *p; p++) {
        /* Returns nonzero if more bytes are needed */
        if (utf8decode(&state, &codepoint, *p))
            continue;

        /* Turn off subpixel rendering, which complicates things when
         * mixed with alpha channels */
        const struct fcft_glyph *glyph = NULL;
        CHECKFONT(glyph = fcft_rasterize_char_utf32(font, codepoint, FCFT_SUBPIXEL_NONE), x);
        if (!glyph)
            continue;

        /* Adjust x position based on kerning with previous glyph */
        long kern = 0;
        if (last_cp) {
            CHECKFONT( fcft_kerning(font, last_cp, codepoint, &kern, NULL), x );
        }
        if ((nx = x + kern + glyph->advance.x) + padding > max_x)
            break;
        last_cp = codepoint;
        x += kern;

        if (draw_fg) {
            /* Detect and handle pre-rendered glyphs (e.g. emoji) */
            if (pixman_image_get_format(glyph->pix) == PIXMAN_a8r8g8b8) {
                /* Only the alpha channel of the mask is used, so we can
                 * use fgfill here to blend prerendered glyphs with the
                 * same opacity */
                pixman_image_composite32(
                    PIXMAN_OP_OVER, glyph->pix, fg_fill, foreground, 0, 0, 0, 0,
                    x + glyph->x, y - glyph->y, glyph->width, glyph->height);
            } else {
                /* Applying the foreground color here would mess up
                 * component alphas for subpixel-rendered text, so we
                 * apply it when blending. */
                pixman_image_composite32(
                    PIXMAN_OP_OVER, fg_fill, glyph->pix, foreground, 0, 0, 0, 0,
                    x + glyph->x, y - glyph->y, glyph->width, glyph->height);
            }
        }

        if (draw_bg) {
            pixman_image_fill_boxes(PIXMAN_OP_OVER, background,
                        cur_bg_color, 1, &(pixman_box32_t){
                            .x1 = x, .x2 = nx,
                            .y1 = ymin-padding, .y2 = ymax+padding,
                        });
        }

        /* increment pen position */
        x = nx;
    }

    if (draw_fg)
        pixman_image_unref(fg_fill);
    if (!last_cp)
        return ix;

    nx = x + padding;
    if (draw_bg) {
        /* Fill padding background */
        pixman_image_fill_boxes(PIXMAN_OP_OVER, background,
                    bg_color, 1, &(pixman_box32_t){
                        .x1 = ix, .x2 = ix + padding,
                        .y1 = ymin-padding, .y2 = ymax+padding,
                    });
        pixman_image_fill_boxes(PIXMAN_OP_OVER, background,
                    bg_color, 1, &(pixman_box32_t){
                        .x1 = x, .x2 = nx,
                        .y1 = ymin-padding, .y2 = ymax+padding,
                    });
    }

    return nx;
}


uint32_t draw_text(char *text,
      uint32_t x,
      uint32_t y,
      pixman_image_t *foreground,
      pixman_image_t *background,
      pixman_color_t *fg_color,
      pixman_color_t *bg_color,
      uint32_t max_x,
      uint32_t buf_height,
      uint32_t padding ) {

    if (!font) return x;

    if (!text || !*text || !max_x)
        return x;

    uint32_t ix = x, nx;

    if ((nx = x + padding) + padding >= max_x)
        return x;
    x = nx;

    bool draw_fg = foreground && fg_color;
    bool draw_bg = background && bg_color;

    pixman_image_t *fg_fill = NULL;
    pixman_color_t *cur_bg_color = NULL;
    if (draw_fg)
        fg_fill = pixman_image_create_solid_fill(fg_color);
    if (draw_bg)
        cur_bg_color = bg_color;

    uint32_t codepoint, state = UTF8_ACCEPT, last_cp = 0;
    for (char *p = text; *p; p++) {
        /* Returns nonzero if more bytes are needed */
        if (utf8decode(&state, &codepoint, *p))
            continue;

        /* Turn off subpixel rendering, which complicates things when
         * mixed with alpha channels */
        const struct fcft_glyph *glyph = NULL;
        CHECKFONT( glyph = fcft_rasterize_char_utf32(font, codepoint, FCFT_SUBPIXEL_NONE), x );
        if (!glyph)
            continue;

        /* Adjust x position based on kerning with previous glyph */
        long kern = 0;
        if (last_cp) {
            CHECKFONT( fcft_kerning(font, last_cp, codepoint, &kern, NULL), x );
        }
        if ((nx = x + kern + glyph->advance.x) + padding > max_x)
            break;
        last_cp = codepoint;
        x += kern;

        if (draw_fg) {
            /* Detect and handle pre-rendered glyphs (e.g. emoji) */
            if (pixman_image_get_format(glyph->pix) == PIXMAN_a8r8g8b8) {
                /* Only the alpha channel of the mask is used, so we can
                 * use fgfill here to blend prerendered glyphs with the
                 * same opacity */
                pixman_image_composite32(
                    PIXMAN_OP_OVER, glyph->pix, fg_fill, foreground, 0, 0, 0, 0,
                    x + glyph->x, y - glyph->y, glyph->width, glyph->height);
            } else {
                /* Applying the foreground color here would mess up
                 * component alphas for subpixel-rendered text, so we
                 * apply it when blending. */
                pixman_image_composite32(
                    PIXMAN_OP_OVER, fg_fill, glyph->pix, foreground, 0, 0, 0, 0,
                    x + glyph->x, y - glyph->y, glyph->width, glyph->height);
            }
        }

        if (draw_bg) {
            pixman_image_fill_boxes(PIXMAN_OP_OVER, background,
                        cur_bg_color, 1, &(pixman_box32_t){
                            .x1 = x, .x2 = nx,
                            .y1 = 0, .y2 = buf_height
                        });
        }

        /* increment pen position */
        x = nx;
    }

    if (draw_fg)
        pixman_image_unref(fg_fill);
    if (!last_cp)
        return ix;

    nx = x + padding;
    if (draw_bg) {
        /* Fill padding background */
        pixman_image_fill_boxes(PIXMAN_OP_OVER, background,
                    bg_color, 1, &(pixman_box32_t){
                        .x1 = ix, .x2 = ix + padding,
                        .y1 = 0, .y2 = buf_height
                    });
        pixman_image_fill_boxes(PIXMAN_OP_OVER, background,
                    bg_color, 1, &(pixman_box32_t){
                        .x1 = x, .x2 = nx,
                        .y1 = 0, .y2 = buf_height
                    });
    }

    return nx;
}

static uint32_t tagwidget_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background );
static void tagwidget_scroll( Bar* bar, uint32_t pointer_x, int amount );
static void tagwidget_click( Bar* bar, uint32_t pointer_x, int button );

static uint32_t layoutwidget_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background );
static void layoutwidget_scroll( Bar* bar, uint32_t pointer_x, int amount );
static void layoutwidget_click( Bar* bar, uint32_t pointer_x, int button );

static uint32_t clockwidget_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background );
static void clockwidget_view( Bar* bar, int32_t x );
static void clockwidget_click( Bar* bar, uint32_t pointer_x, int button );
static void clockwidget_scroll( Bar* bar, uint32_t pointer_x, int amount );

static uint32_t pulsewidget_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background );
static void pulsewidget_scroll( Bar* bar, uint32_t pointer_x, int amount );
static void pulsewidget_click( Bar* bar, uint32_t pointer_x, int button );

static uint32_t ipwidget_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background );

static uint32_t tempwidget_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background );

#ifndef AWL_SKIP_BATWIDGET
static uint32_t batwidget_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background );
#endif

static uint32_t separator_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background );
static uint32_t systray_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background );

static uint32_t statuswidget_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background );
static void statuswidget_click( Bar* bar, uint32_t pointer_x, int button );

static uint32_t taskbarwidget_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background );
static void taskbarwidget_scroll( Bar* bar, uint32_t pointer_x, int amount );
static void taskbarwidget_click( Bar* bar, uint32_t pointer_x, int button );

static int draw_frame(Bar *bar) {
    sem_wait(&bar->draw_sem);
    int result = 0;
    /* Allocate buffer to be attached to the surface */
    int fd = allocate_shm_file(bar->bufsize);
    if (fd == -1) {
        result = -1;
        goto draw_frame_end;
    }

    uint32_t *data = mmap(NULL, bar->bufsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        result = -1;
        goto draw_frame_end;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, bar->bufsize);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, bar->width, bar->height, bar->stride, WL_SHM_FORMAT_ARGB8888);
    wl_buffer_add_listener(buffer, &wl_buffer_listener, NULL);
    wl_shm_pool_destroy(pool);
    close(fd);

    // pixman image
    pixman_image_t *final = pixman_image_create_bits(PIXMAN_a8r8g8b8, bar->width, bar->height, data, bar->width * 4);

    // text layers
    pixman_image_t *foreground = pixman_image_create_bits(PIXMAN_a8r8g8b8, bar->width,
            bar->height, NULL, bar->width * 4);
    pixman_image_t *background = pixman_image_create_bits(PIXMAN_a8r8g8b8, bar->width,
            bar->height, NULL, bar->width * 4);

    uint32_t x = 0;
    for (int w=0; w<bar->n_widgets_left; ++w) {
        if (bar->widgets_left[w].draw)
            bar->widgets_left[w].width = bar->widgets_left[w].draw( bar, x, foreground, background );
        x += bar->widgets_left[w].width;
    }

    uint32_t x_end = bar->width;
    for (int w=0; w<bar->n_widgets_right; ++w) {
        if (bar->widgets_right[w].draw)
            bar->widgets_right[w].width = bar->widgets_right[w].draw( bar, x_end - bar->widgets_right[w].width,
                    foreground, background );
        x_end -= bar->widgets_right[w].width;
    }

    bar->center_widget_space = x_end - x;
    bar->center_widget_start = x;
    if (bar->has_center_widget) {
        if (bar->center_widget.draw)
            bar->center_widget.draw( bar, x, foreground, background );
    } else {
        pixman_image_fill_boxes(PIXMAN_OP_SRC, background, &black, 1,
                &(pixman_box32_t){ .x1 = x, .x2 = x_end, .y1 = 0, .y2 = bar->height });
    }

    /* Draw background and foreground on bar */
    pixman_image_composite32(PIXMAN_OP_OVER, background, NULL, final, 0, 0, 0, 0, 0, 0, bar->width, bar->height);
    pixman_image_composite32(PIXMAN_OP_OVER, foreground, NULL, final, 0, 0, 0, 0, 0, 0, bar->width, bar->height);

    pixman_image_unref(foreground);
    pixman_image_unref(background);
    pixman_image_unref(final);

    munmap(data, bar->bufsize);

    wl_surface_set_buffer_scale(bar->wl_surface, buffer_scale);
    wl_surface_attach(bar->wl_surface, buffer, 0, 0);
    wl_surface_damage_buffer(bar->wl_surface, 0, 0, bar->width, bar->height);
    wl_surface_commit(bar->wl_surface);

draw_frame_end:
    sem_post(&bar->draw_sem);
    return result;
}

/* Layer-surface setup adapted from layer-shell example in [wlroots] */
static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
            uint32_t serial, uint32_t w, uint32_t h) {
    w = w * buffer_scale;
    h = h * buffer_scale;

    zwlr_layer_surface_v1_ack_configure(surface, serial);

    Bar *bar = (Bar *)data;

    if (bar->configured && w == bar->width && h == bar->height)
        return;

    bar->width = w;
    bar->height = h;
    bar->stride = bar->width * 4;
    bar->bufsize = bar->stride * bar->height;
    bar->configured = true;

    draw_frame(bar);
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface) {
    (void)surface;
    Bar* bar = (Bar*)data;
    atomic_store( &bar->handle->running, 0 );
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_configure,
    .closed = layer_surface_closed,
};

static void output_name(void *data, struct zxdg_output_v1 *xdg_output, const char *name) {
    (void)xdg_output;
    Bar *bar = (Bar *)data;

    if (bar->xdg_output_name)
        free(bar->xdg_output_name);
    bar->xdg_output_name = strdup(name);
}

static void output_logical_position(void *data, struct zxdg_output_v1 *xdg_output,
            int32_t x, int32_t y) {
    (void)data;
    (void)xdg_output;
    (void)x;
    (void)y;
}

static void output_logical_size(void *data, struct zxdg_output_v1 *xdg_output,
            int32_t width, int32_t height) {
    (void)data;
    (void)xdg_output;
    (void)width;
    (void)height;
}

static void output_done(void *data, struct zxdg_output_v1 *xdg_output) {
    (void)data;
    (void)xdg_output;
}

static void output_description(void *data, struct zxdg_output_v1 *xdg_output,
           const char *description) {
    (void)data;
    (void)xdg_output;
    (void)description;
}

static const struct zxdg_output_v1_listener output_listener = {
    .name = output_name,
    .logical_position = output_logical_position,
    .logical_size = output_logical_size,
    .done = output_done,
    .description = output_description
};

static void pointer_enter(void *data, struct wl_pointer *pointer,
          uint32_t serial, struct wl_surface *surface,
          wl_fixed_t surface_x, wl_fixed_t surface_y) {
    (void)surface_x;
    (void)surface_y;
    Seat *seat = (Seat *)data;

    seat->bar = NULL;
    Bar *bar;
    DL_FOREACH(bar_list, bar) {
        if (bar->wl_surface == surface) {
            seat->bar = bar;
            break;
        }
    }

    if (!cursor_image) {
        const char *size_str = getenv("XCURSOR_SIZE");
        int size = size_str ? atoi(size_str) : 0;
        if (size == 0)
            size = 24;
        struct wl_cursor_theme *cursor_theme = wl_cursor_theme_load(getenv("XCURSOR_THEME"),
                size * buffer_scale, shm);
        cursor_image = wl_cursor_theme_get_cursor(cursor_theme, "left_ptr")->images[0];
        cursor_surface = wl_compositor_create_surface(compositor);
        wl_surface_set_buffer_scale(cursor_surface, buffer_scale);
        wl_surface_attach(cursor_surface, wl_cursor_image_get_buffer(cursor_image), 0, 0);
        wl_surface_commit(cursor_surface);
    }
    wl_pointer_set_cursor(pointer, serial, cursor_surface,
                  cursor_image->hotspot_x,
                  cursor_image->hotspot_y);
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
          uint32_t serial, struct wl_surface *surface) {
    (void)pointer;
    (void)serial;
    (void)surface;
    Seat *seat = (Seat *)data;
    Bar* bar = seat->bar;
    if (!bar) return;

    if (bar->n_widgets_right > 0 && bar->widgets_right[0].callback_view)
        bar->widgets_right[0].callback_view( bar, -1 );
    if (bar->center_widget.callback_view)
        bar->center_widget.callback_view( bar, -1 );

    seat->bar = NULL;
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
           uint32_t time, uint32_t button, uint32_t state) {
    (void)pointer;
    (void)serial;
    (void)time;
    Seat *seat = (Seat *)data;

    seat->pointer_button = state == WL_POINTER_BUTTON_STATE_PRESSED ? button : 0;
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
           wl_fixed_t surface_x, wl_fixed_t surface_y) {
    (void)pointer;
    (void)time;
    Seat *seat = (Seat *)data;
    Bar* bar = seat->bar;
    if (!bar) return;

    seat->pointer_x = wl_fixed_to_int(surface_x);
    seat->pointer_y = wl_fixed_to_int(surface_y);

    // center widget
    if (bar->has_center_widget && bar->center_widget.callback_view &&
        seat->pointer_x >= bar->center_widget_start/buffer_scale &&
        seat->pointer_x <= (bar->center_widget_start+bar->center_widget_space)/buffer_scale)
            bar->center_widget.callback_view( bar, seat->pointer_x - bar->center_widget_start/buffer_scale );

    // rightmost widget
    if (bar->n_widgets_right > 0 && bar->widgets_right[0].callback_view) {
        int32_t x = bar->width/buffer_scale - bar->widgets_right[0].width/buffer_scale;
        bar->widgets_right[0].callback_view( bar, seat->pointer_x - x );
    }
}

static double continuous_event = 0.0;
static const double continuous_event_norm = 10.0;

static void pointer_frame(void *data, struct wl_pointer *pointer) {
    (void)pointer;
    if (!data) return;
    Seat *seat = (Seat *)data;
    Bar* bar = seat->bar;
    if (!bar) return;
    int discrete_event = 0;
    struct pointer_event* event = &seat->pointer_event;
    if (event->event_mask & (POINTER_EVENT_AXIS_DISCRETE|POINTER_EVENT_AXIS) &&
        event->axes[WL_POINTER_AXIS_VERTICAL_SCROLL].valid) {
        discrete_event = event->axes[WL_POINTER_AXIS_VERTICAL_SCROLL].discrete;
        if (!discrete_event) {
            continuous_event += wl_fixed_to_double(event->axes[WL_POINTER_AXIS_VERTICAL_SCROLL].value) /
                                continuous_event_norm;
            if (fabs(continuous_event) > 1.0) {
                discrete_event = continuous_event;
                continuous_event = 0.0;
            }
        }
    }
    memset(event, 0, sizeof(*event));
    if (discrete_event) {
        // left widgets
        uint32_t x = 0;
        for (int w=0; w<bar->n_widgets_left; ++w) {
            if (seat->pointer_x >= x && seat->pointer_x <= x + bar->widgets_left[w].width/buffer_scale) {
                if (bar->widgets_left[w].callback_scroll)
                    bar->widgets_left[w].callback_scroll( bar, seat->pointer_x - x, discrete_event );
                return;
            }
            x += bar->widgets_left[w].width/buffer_scale;
        }
        // save start pos
        uint32_t c = x;
        // right widgets
        x = bar->width/buffer_scale;
        for (int w=0; w<bar->n_widgets_right; ++w) {
            x -= bar->widgets_right[w].width/buffer_scale;
            if (seat->pointer_x >= x && seat->pointer_x <= x + bar->widgets_right[w].width/buffer_scale) {
                if (bar->widgets_right[w].callback_scroll)
                    bar->widgets_right[w].callback_scroll( bar, seat->pointer_x - x, discrete_event );
                return;
            }
        }
        // center widget
        if (bar->has_center_widget && bar->center_widget.callback_scroll)
            bar->center_widget.callback_scroll( bar, seat->pointer_x - c, discrete_event );
        return;
    }

    if (seat->pointer_button) {
        // left widgets
        uint32_t x = 0;
        for (int w=0; w<bar->n_widgets_left; ++w) {
            if (seat->pointer_x >= x && seat->pointer_x <= x + bar->widgets_left[w].width/buffer_scale) {
                if (bar->widgets_left[w].callback_click)
                    bar->widgets_left[w].callback_click( bar, seat->pointer_x - x, seat->pointer_button );
                seat->pointer_button = 0; return;
            }
            x += bar->widgets_left[w].width/buffer_scale;
        }
        // save start pos
        uint32_t c = x;
        // right widgets
        x = bar->width/buffer_scale;
        for (int w=0; w<bar->n_widgets_right; ++w) {
            x -= bar->widgets_right[w].width/buffer_scale;
            if (seat->pointer_x >= x && seat->pointer_x <= x + bar->widgets_right[w].width/buffer_scale) {
                if (bar->widgets_right[w].callback_click)
                    bar->widgets_right[w].callback_click( bar, seat->pointer_x - x, seat->pointer_button );
                seat->pointer_button = 0; return;
            }
        }
        // center widget
        if (bar->has_center_widget && bar->center_widget.callback_click)
            bar->center_widget.callback_click( bar, seat->pointer_x - c, seat->pointer_button );
        seat->pointer_button = 0; return;
    }

}

static void pointer_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time,
               uint32_t axis, wl_fixed_t value) {
    (void)wl_pointer;
    Seat *seat = (Seat*)data;
    seat->pointer_event.event_mask |= POINTER_EVENT_AXIS;
    seat->pointer_event.time = time;
    seat->pointer_event.axes[axis].valid = true;
    seat->pointer_event.axes[axis].value = value;
}

static void pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
               uint32_t axis_source) {
    (void)wl_pointer;
    Seat *seat = (Seat*)data;
    seat->pointer_event.event_mask |= POINTER_EVENT_AXIS_SOURCE;
    seat->pointer_event.axis_source = axis_source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
               uint32_t time, uint32_t axis) {
    (void)wl_pointer;
    Seat *seat = (Seat*)data;
    seat->pointer_event.time = time;
    seat->pointer_event.event_mask |= POINTER_EVENT_AXIS_STOP;
    seat->pointer_event.axes[axis].valid = true;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
               uint32_t axis, int32_t discrete) {
    (void)wl_pointer;
    Seat *seat = (Seat*)data;
    seat->pointer_event.event_mask |= POINTER_EVENT_AXIS_DISCRETE;
    seat->pointer_event.axes[axis].valid = true;
    seat->pointer_event.axes[axis].discrete = discrete;
}

static void pointer_axis_value120(void *data, struct wl_pointer *pointer,
              uint32_t axis, int32_t discrete) {
    (void)data;
    (void)pointer;
    (void)axis;
    (void)discrete;
}

static const struct wl_pointer_listener pointer_listener = {
    .axis = pointer_axis,
    .axis_discrete = pointer_axis_discrete,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_value120 = pointer_axis_value120,
    .button = pointer_button,
    .enter = pointer_enter,
    .frame = pointer_frame,
    .leave = pointer_leave,
    .motion = pointer_motion,
};

static void seat_capabilities(void *data, struct wl_seat *wl_seat,
          uint32_t capabilities) {
    (void)wl_seat;
    Seat *seat = (Seat *)data;

    uint32_t has_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;
    if (has_pointer && !seat->wl_pointer) {
        seat->wl_pointer = wl_seat_get_pointer(seat->wl_seat);
        wl_pointer_add_listener(seat->wl_pointer, &pointer_listener, seat);
    } else if (!has_pointer && seat->wl_pointer) {
        wl_pointer_destroy(seat->wl_pointer);
        seat->wl_pointer = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *wl_seat, const char *name) {
    (void)data;
    (void)wl_seat;
    (void)name;
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

static void show_bar(Bar *bar) {
    bar->wl_surface = wl_compositor_create_surface(compositor);
    if (!bar->wl_surface)
        P_awl_err_printf( "Could not create wl_surface" );

    bar->layer_surface = zwlr_layer_shell_v1_get_layer_surface(layer_shell, bar->wl_surface, bar->wl_output,
                                   ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, "awl_bar");
    if (!bar->layer_surface)
        P_awl_err_printf( "Could not create layer_surface" );
    zwlr_layer_surface_v1_add_listener(bar->layer_surface, &layer_surface_listener, bar);

    zwlr_layer_surface_v1_set_size(bar->layer_surface, 0, bar->height / buffer_scale);
    zwlr_layer_surface_v1_set_anchor(bar->layer_surface, ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT|ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(bar->layer_surface, bar->height / buffer_scale);
    wl_surface_commit(bar->wl_surface);

    bar->hidden = false;
}

static void hide_bar(Bar *bar) {
    zwlr_layer_surface_v1_destroy(bar->layer_surface);
    wl_surface_destroy(bar->wl_surface);

    bar->configured = false;
    bar->hidden = true;
}

static void dwl_wm_tags(void *data, struct zdwl_ipc_manager_v2 *dwl_wm, uint32_t amount) {
    (void)data;
    (void)dwl_wm;
    for (uint32_t i=0; i<amount; i++)
        STATIC_ARRAY_APPEND_STR( tags, tags_names[MIN(i, n_tags_names-1)] );
}

static void dwl_wm_layout(void *data, struct zdwl_ipc_manager_v2 *dwl_wm, const char *name) {
    (void)data;
    (void)dwl_wm;
    STATIC_ARRAY_APPEND_STR( layouts, name );
}

static const struct zdwl_ipc_manager_v2_listener dwl_wm_listener = {
    .tags = dwl_wm_tags,
    .layout = dwl_wm_layout
};

static void dwl_wm_output_toggle_visibility(void *data, struct zdwl_ipc_output_v2 *dwl_wm_output) {
    (void)dwl_wm_output;
    Bar *bar = (Bar *)data;

    if (bar->hidden)
        show_bar(bar);
    else
        hide_bar(bar);
}

static void dwl_wm_output_active(void *data, struct zdwl_ipc_output_v2 *dwl_wm_output, uint32_t active) {
    (void)dwl_wm_output;
    Bar *bar = (Bar *)data;

    if (active != bar->sel)
        bar->sel = active;
}

static void dwl_wm_output_tag(void *data, struct zdwl_ipc_output_v2 *dwl_wm_output,
    uint32_t tag, uint32_t state, uint32_t clients, uint32_t focused) {
    (void)dwl_wm_output;
    (void)focused;
    Bar *bar = (Bar *)data;

    if (state & ZDWL_IPC_OUTPUT_V2_TAG_STATE_ACTIVE)
        bar->mtags |= 1 << tag;
    else
        bar->mtags &= ~(1 << tag);
    if (clients > 0)
        bar->ctags |= 1 << tag;
    else
        bar->ctags &= ~(1 << tag);
    if (state & ZDWL_IPC_OUTPUT_V2_TAG_STATE_URGENT)
        bar->urg |= 1 << tag;
    else
        bar->urg &= ~(1 << tag);
}

static void dwl_wm_output_layout(void *data, struct zdwl_ipc_output_v2 *dwl_wm_output,
        uint32_t layout) {
    (void)dwl_wm_output;
    Bar *bar = (Bar *)data;
    bar->layout_idx = layout;
}

static void dwl_wm_output_title_ary(void *data, struct zdwl_ipc_output_v2 *dwl_wm_output,
    struct wl_array* ary) {
    (void)dwl_wm_output;

    Bar *bar = (Bar *)data;

    awl_title_t* titles = (awl_title_t*)ary->data;
    bar->window_titles = realloc(bar->window_titles, ary->size);
    bar->n_window_titles = ary->size / sizeof(awl_title_t);
    if (bar->window_titles && titles)
        memcpy(bar->window_titles, titles, ary->size);
}

static void dwl_wm_output_title(void *data, struct zdwl_ipc_output_v2 *dwl_wm_output,
    const char *title) {
    (void)data;
    (void)dwl_wm_output;
    (void)title;
}

static void dwl_wm_output_appid(void *data, struct zdwl_ipc_output_v2 *dwl_wm_output,
    const char *appid) {
    (void)data;
    (void)dwl_wm_output;
    (void)appid;
}

static void dwl_wm_output_layout_symbol(void *data, struct zdwl_ipc_output_v2 *dwl_wm_output,
    const char *layout) {
    (void)dwl_wm_output;
    Bar *bar = (Bar *)data;
    strncpy( layouts[bar->layout_idx], layout, 15 );
    bar->layout = layouts[bar->layout_idx];
}

static void dwl_wm_output_frame(void *data, struct zdwl_ipc_output_v2 *dwl_wm_output) {
    (void)dwl_wm_output;
    Bar *bar = (Bar *)data;
    bar->redraw = true;
}

static void dwl_wm_output_fullscreen(void *data, struct zdwl_ipc_output_v2 *dwl_wm_output,
    uint32_t is_fullscreen) {
    (void)data;
    (void)dwl_wm_output;
    (void)is_fullscreen;
}

static void dwl_wm_output_floating(void *data, struct zdwl_ipc_output_v2 *dwl_wm_output,
    uint32_t is_floating) {
    (void)data;
    (void)dwl_wm_output;
    (void)is_floating;
}

static const struct zdwl_ipc_output_v2_listener dwl_wm_output_listener = {
    .toggle_visibility = dwl_wm_output_toggle_visibility,
    .active = dwl_wm_output_active,
    .tag = dwl_wm_output_tag,
    .layout = dwl_wm_output_layout,
    .title = dwl_wm_output_title,
    .title_ary = dwl_wm_output_title_ary,
    .appid = dwl_wm_output_appid,
    .layout_symbol = dwl_wm_output_layout_symbol,
    .frame = dwl_wm_output_frame,
    .fullscreen = dwl_wm_output_fullscreen,
    .floating = dwl_wm_output_floating
};

static void setup_bar(Bar *bar) {
    awl_plugin_data_t* P = awl_plugin_data();
    if (!P)
        P_awl_err_printf("cannot init bar without plugin data");

    bar->height = height * buffer_scale;
    bar->textpadding = textpadding;
    bar->hidden = 0;
    bar->window_titles = NULL;
    bar->n_window_titles = 0;
    bar->window_list = NULL;
    bar->n_window_list = 0;
    bar->cpy_window_list = NULL;
    bar->cpy_n_window_list = 0;
    bar->layout = default_layout_name;
    sem_init( &bar->draw_sem, 0, 1 );

    bar->n_widgets_left = bar->n_widgets_right = bar->has_center_widget = 0;

    // widgets aligned to the left
    bar->widgets_left[bar->n_widgets_left++] = (widget_t){
        .draw = tagwidget_draw,
        .callback_scroll = tagwidget_scroll,
        .callback_click = tagwidget_click,
    };
    bar->widgets_left[bar->n_widgets_left++] = (widget_t){
        .draw = layoutwidget_draw,
        .callback_scroll = layoutwidget_scroll,
        .callback_click = layoutwidget_click,
    };

    // widgets aligned to the right
    bar->widgets_right[bar->n_widgets_right++] = (widget_t){
        .draw = clockwidget_draw,
        .callback_view = clockwidget_view,
        .callback_click = clockwidget_click,
        .callback_scroll = clockwidget_scroll,
        .width = TEXT_WIDTH( "XX:XX", -1, bar->textpadding ),
    };
    bar->widgets_right[bar->n_widgets_right++] = (widget_t){
        .draw = separator_draw,
        .width = TEXT_WIDTH( "|", -1, 0 ),
    };
    bar->widgets_right[bar->n_widgets_right++] = (widget_t){
        .draw = systray_draw,
        .width = 128,
    };
    bar->widgets_right[bar->n_widgets_right++] = (widget_t){
        .draw = separator_draw,
        .width = TEXT_WIDTH( "|", -1, 0 ),
    };
    bar->widgets_right[bar->n_widgets_right++] = (widget_t){
        .draw = pulsewidget_draw,
        .width = TEXT_WIDTH( "100%", -1, bar->textpadding ),
        .callback_scroll = pulsewidget_scroll,
        .callback_click = pulsewidget_click,
    };
    bar->widgets_right[bar->n_widgets_right++] = (widget_t){
        .draw = statuswidget_draw,
        .width = P->stats->ncpu + P->stats->nmem + P->stats->nswp,
        .callback_click = statuswidget_click,
    };
    bar->widgets_right[bar->n_widgets_right++] = (widget_t){
        .draw = tempwidget_draw,
        .width = TEXT_WIDTH( "CPU:45°C", -1, bar->textpadding ),
    };
    bar->widgets_right[bar->n_widgets_right++] = (widget_t){
        .draw = separator_draw,
        .width = TEXT_WIDTH( "|", -1, 0 ),
    };
    #ifndef AWL_SKIP_BATWIDGET
    bar->widgets_right[bar->n_widgets_right++] = (widget_t){
        .draw = batwidget_draw,
        .width = TEXT_WIDTH( "100%", -1, bar->textpadding ),
    };
    bar->widgets_right[bar->n_widgets_right++] = (widget_t){
        .draw = separator_draw,
        .width = TEXT_WIDTH( "|", -1, 0 ),
    };
    #endif // AWL_SKIP_BATWIDGET
    bar->widgets_right[bar->n_widgets_right++] = (widget_t){
        .draw = ipwidget_draw,
        .width = TEXT_WIDTH( "ipaddr", -1, bar->textpadding ),
    };

    // central widget
    bar->center_widget = (widget_t){
        .draw = taskbarwidget_draw,
        .callback_scroll = taskbarwidget_scroll,
        .callback_click = taskbarwidget_click,
    };
    bar->has_center_widget = 1;

    bar->xdg_output = zxdg_output_manager_v1_get_xdg_output(output_manager, bar->wl_output);
    if (!bar->xdg_output)
        P_awl_err_printf( "Could not create xdg_output" );
    zxdg_output_v1_add_listener(bar->xdg_output, &output_listener, bar);

    bar->dwl_wm_output = zdwl_ipc_manager_v2_get_output(dwl_wm, bar->wl_output);
    if (!bar->dwl_wm_output)
        P_awl_err_printf( "Could not create dwl_wm_output" );
    zdwl_ipc_output_v2_add_listener(bar->dwl_wm_output, &dwl_wm_output_listener, bar);

    if (!bar->hidden)
        show_bar(bar);
}

static void handle_global(void *data, struct wl_registry *registry,
          uint32_t name, const char *interface, uint32_t version) {
    (void)version;
    awl_bar_handle_t* handle = (awl_bar_handle_t*)data;
    if (!strcmp(interface, wl_compositor_interface.name)) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (!strcmp(interface, wl_shm_interface.name)) {
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name)) {
        layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
    } else if (!strcmp(interface, zxdg_output_manager_v1_interface.name)) {
        output_manager = wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, 2);
    } else if (!strcmp(interface, zdwl_ipc_manager_v2_interface.name)) {
        dwl_wm = wl_registry_bind(registry, name, &zdwl_ipc_manager_v2_interface, 2);
        zdwl_ipc_manager_v2_add_listener(dwl_wm, &dwl_wm_listener, NULL);
    } else if (!strcmp(interface, wl_output_interface.name)) {
        Bar *bar = calloc(1, sizeof(Bar));
        bar->registry_name = name;
        bar->wl_output = wl_registry_bind(registry, name, &wl_output_interface, 1);
        bar->handle = handle;
        if (atomic_load( &handle->running ))
            setup_bar(bar);
        DL_APPEND(bar_list, bar);
    } else if (!strcmp(interface, wl_seat_interface.name)) {
        Seat *seat = calloc(1, sizeof(Seat));
        seat->registry_name = name;
        seat->wl_seat = wl_registry_bind(registry, name, &wl_seat_interface, 7);
        wl_seat_add_listener(seat->wl_seat, &seat_listener, seat);
        DL_APPEND(seat_list, seat);
    }
}

static void teardown_bar(Bar *bar) {
    sem_wait(&bar->draw_sem);
    sem_destroy(&bar->draw_sem);
    if (bar->window_titles)
        free(bar->window_titles);
    zdwl_ipc_output_v2_destroy(bar->dwl_wm_output);
    if (bar->xdg_output_name)
        free(bar->xdg_output_name);
    if (!bar->hidden) {
        zwlr_layer_surface_v1_destroy(bar->layer_surface);
        wl_surface_destroy(bar->wl_surface);
    }
    zxdg_output_v1_destroy(bar->xdg_output);
    wl_output_destroy(bar->wl_output);
    free(bar);
}

static void teardown_seat(Seat *seat) {
    if (seat->wl_pointer)
        wl_pointer_destroy(seat->wl_pointer);
    wl_seat_destroy(seat->wl_seat);
    free(seat);
}

static void handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data;
    (void)registry;
    Bar *bar, *bar_tmp;
    Seat *seat, *seat_tmp;

    P_awl_log_printf("in global remove");
    DL_FOREACH_SAFE(bar_list, bar, bar_tmp) {
        if (bar->registry_name == name) {
            DL_DELETE(bar_list, bar);
            teardown_bar(bar);
            return;
        }
    }
    DL_FOREACH_SAFE(seat_list, seat, seat_tmp) {
        if (seat->registry_name == name) {
            DL_DELETE(seat_list, seat);
            teardown_seat(seat);
            return;
        }
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove
};

static void event_loop( awl_bar_handle_t* h ) {
    P_awl_log_printf( "bar event loop" );
    int wl_fd = wl_display_get_fd(display);

    while (atomic_load(&h->running)) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(wl_fd, &rfds);
        FD_SET(h->redraw_fd, &rfds);

        wl_display_flush(display);

        if (select(MAX(h->redraw_fd,wl_fd)+1, &rfds, NULL, NULL, NULL) == -1) {
            if (errno == EINTR)
                continue;
            else
                P_awl_err_printf( "select" );
        }
        if (FD_ISSET(wl_fd, &rfds))
            if (wl_display_dispatch(display) == -1)
                break;
        if (FD_ISSET(h->redraw_fd, &rfds)) {
            uint64_t val = 0;
            eventfd_read(h->redraw_fd, &val);
        }

        Bar *bar;
        DL_FOREACH(bar_list, bar) {
            if (bar->redraw) {
                if (!bar->hidden && bar->configured)
                    draw_frame(bar);
                bar->redraw = false;
            }
        }
    }
}

void awl_bar_refresh( awl_bar_handle_t* h, int redraw ) {
    sem_wait( &h->bar_init_sem );
    sem_post( &h->bar_init_sem );
    Bar* bar;
    DL_FOREACH(bar_list, bar) {
        bar->redraw = redraw;
    }
    eventfd_write(h->redraw_fd, 1);
}

void awl_bar_stop( awl_bar_handle_t* h ) {
    P_awl_log_printf( "entering bar cleanup" );
    sem_wait( &h->bar_init_sem );
    atomic_store( &h->running, 0 );
    eventfd_write( h->redraw_fd, 1 );
    pthread_join( h->bar, NULL );
    if (!pthread_cancel( h->refresh )) pthread_join( h->refresh, NULL );
    if (h->redraw_fd != -1) close( h->redraw_fd );

    free( widget_boxes ); // TODO this data belongs to someone...

    Bar *bar, *bar2;
    Seat *seat, *seat2;
    DL_FOREACH_SAFE(bar_list, bar, bar2) {
        DL_DELETE(bar_list, bar);
        teardown_bar(bar);
    }
    DL_FOREACH_SAFE(seat_list, seat, seat2) {
        DL_DELETE(seat_list, seat);
        teardown_seat(seat);
    }

    zwlr_layer_shell_v1_destroy(layer_shell);
    zxdg_output_manager_v1_destroy(output_manager);
    zdwl_ipc_manager_v2_destroy(dwl_wm);

    for (int i=0; i<font_sem_nusers; ++i) sem_wait( &font_sem );
    fcft_destroy(font);
    font = NULL;
    fcft_fini();
    sem_destroy( &font_sem );

    wl_shm_destroy(shm);
    wl_compositor_destroy(compositor);
    wl_display_disconnect(display);

    free( h );
    P_awl_log_printf( "done with bar cleanup" );
}

static void* awl_bar_run_async( void* arg );
static void* awl_bar_refresher( void* arg );

awl_bar_handle_t* awl_bar_run( float refresh_sec ) {
    P_awl_log_printf( "starting bar thread" );
    awl_bar_handle_t* h = calloc(1, sizeof(awl_bar_handle_t));
    h->refresh_sec = refresh_sec;
    sem_init( &h->bar_init_sem, 0, 1 );
    sem_wait( &h->bar_init_sem );

    AWL_PTHREAD_CREATE( &h->bar, NULL, awl_bar_run_async, h );
    AWL_PTHREAD_CREATE( &h->refresh, NULL, awl_bar_refresher, h );
    return h;
}

static void* awl_bar_run_async( void* arg ) {
    awl_bar_handle_t* h = (awl_bar_handle_t*)(arg);
    awl_state_t* B = awl_plugin_state();
    if (!B) return NULL;
    sem_wait( B->awl_is_ready_sem() );
    sem_post( B->awl_is_ready_sem() );

    /* Set up display and protocols */
    display = wl_display_connect(NULL);
    if (!display)
        P_awl_err_printf( "Failed to create display" );

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, h);
    wl_display_roundtrip(display);
    if (!compositor || !shm || !layer_shell || !output_manager || !dwl_wm)
        P_awl_err_printf( "Compositor does not support all needed protocols" );

    sem_init( &font_sem, 0, 1 );
    sem_wait( &font_sem );
    /* Load selected font */
    fcft_init(FCFT_LOG_COLORIZE_AUTO, 0, FCFT_LOG_CLASS_ERROR);
    fcft_set_scaling_filter(FCFT_SCALING_FILTER_LANCZOS3);

    unsigned int dpi = 96 * buffer_scale;
    char buf[16];
    sprintf(buf, "dpi=%u", dpi);
    if (!(font = fcft_from_name(1, (const char *[]){fontstr}, buf)))
        P_awl_err_printf( "Could not load font %s", fontstr );
    textpadding = font->height / 2;
    height = font->height / buffer_scale + vertical_padding * 2;
    for (int i=0; i<font_sem_nusers; ++i) sem_post( &font_sem );

    /* Setup bars */
    Bar* bar;
    DL_FOREACH(bar_list, bar) {
        setup_bar(bar);
    }
    wl_display_roundtrip(display);

    h->redraw_fd = eventfd(0,0);
    sem_post( &h->bar_init_sem );
    atomic_init( &h->running, 1 );
    event_loop( h );
    return NULL;
}

static void* awl_bar_refresher( void* arg ) {
    awl_bar_handle_t* h = (awl_bar_handle_t*)arg;
    sem_wait( &h->bar_init_sem );
    sem_post( &h->bar_init_sem );
    P_awl_log_printf( "started bar refresh thread (%.2fs)", h->refresh_sec );
    while (true) {
        usleep( (useconds_t)(h->refresh_sec * 1.e6) );
        Bar* bar;
        DL_FOREACH(bar_list, bar) {
            bar->redraw = true;
        }
        eventfd_write(h->redraw_fd, 1);
    }
    return NULL;
}

// widget implementations

static uint32_t tagwidget_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background ) {
    uint32_t x_enter = x;
    uint32_t y = (bar->height + font->ascent - font->descent) / 2;
    uint32_t boxs = font->height / 9;
    uint32_t boxw = font->height / 6 + 2;
    // draw tags
    for (int i = 0; i < n_tags; i++) {
        bool active = bar->mtags & 1 << i;
        bool occupied = bar->ctags & 1 << i;
        bool urgent = bar->urg & 1 << i;
        // draw small box
        if (occupied) {
            pixman_image_fill_boxes(PIXMAN_OP_SRC, foreground,
                        &barcolors.fg_tags, 1, &(pixman_box32_t){
                            .x1 = x + boxs, .x2 = x + boxs + boxw,
                            .y1 = boxs, .y2 = boxs + boxw
                        });
            if ((!bar->sel || !active) && boxw >= 3) {
                /* Make box hollow */
                pixman_image_fill_boxes(PIXMAN_OP_SRC, foreground,
                            &(pixman_color_t){ 0 },
                            1, &(pixman_box32_t){
                                .x1 = x + boxs + 1, .x2 = x + boxs + boxw - 1,
                                .y1 = boxs + 1, .y2 = boxs + boxw - 1
                            });
            }
        }
        // find the right bg color (maybe do that with fg too?)
        pixman_color_t bg = barcolors.bg_tags;
        if (occupied) bg = alpha_blend_16( bg, barcolors.bg_tags_occ );
        if (active) bg = alpha_blend_16( bg, barcolors.bg_tags_act );
        if (urgent) bg = alpha_blend_16( bg, barcolors.bg_tags_urg );
        x = draw_text(tags[i], x, y, foreground, background, &barcolors.fg_tags, &bg,
                bar->width, bar->height, bar->textpadding);
    }
    return x - x_enter;
}

static void tagwidget_scroll( Bar* bar, uint32_t pointer_x, int amount ) {
    awl_plugin_data_t* P = awl_plugin_data();
    if (!P) return;

    uint32_t x = 0;
    int i = 0;
    do {
        x += TEXT_WIDTH(tags[i], bar->width, bar->textpadding) / buffer_scale;
    } while (pointer_x >= x && ++i < n_tags);
    if (i < n_tags) {
        P->cycle_tag( &( (const Arg){.i=amount} ) );
        bar->redraw = true;
    }
}

static void tagwidget_click( Bar* bar, uint32_t pointer_x, int button ) {
    awl_plugin_data_t* P = awl_plugin_data();
    if (!P) return;

    uint32_t x = 0;
    int i = 0;
    do {
        x += TEXT_WIDTH(tags[i], bar->width, bar->textpadding) / buffer_scale;
    } while (pointer_x >= x && ++i < n_tags);
    if (i < n_tags) {
        if (button == BTN_LEFT) {
            if (P->view) (*P->view)( &( (const Arg){.ui=1<<i} ) );
        } else if (button == BTN_MIDDLE) {
            if (P->view) (*P->view)( &( (const Arg){.ui=~0} ) );
        } else if (button == BTN_RIGHT) {
            if (P->toggleview) (*P->toggleview)( &( (const Arg){.ui=1<<i} ) );
        }
    }
}

static uint32_t layoutwidget_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background ) {
    uint32_t y = (bar->height + font->ascent - font->descent) / 2;
    uint32_t new_x = draw_text(bar->layout, x, y, foreground, background,
              &barcolors.fg_lay, &barcolors.bg_lay, bar->width,
              bar->height, bar->textpadding);
    return new_x - x;
}

static void layoutwidget_scroll( Bar* bar, uint32_t pointer_x, int amount ) {
    (void)bar;
    (void)pointer_x;
    awl_plugin_data_t* P = awl_plugin_data();
    if (!P) return;
    P->cycle_layout( &( (const Arg){.i=amount} ) );
    bar->redraw = true;
}

static void layoutwidget_click( Bar* bar, uint32_t pointer_x, int button ) {
    (void)bar;
    (void)pointer_x;
    awl_plugin_data_t* P = awl_plugin_data();
    if (!P) return;
    switch (button) {
        case BTN_LEFT: P->cycle_layout( &( (const Arg){.i=1} ) ); break;
        case BTN_RIGHT: P->cycle_layout( &( (const Arg){.i=-1} ) ); break;
        default: break;
    }
    bar->redraw = true;
}

static uint32_t clockwidget_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background ) {
    awl_plugin_data_t* P = awl_plugin_data();
    if (!P) return 0;

    uint32_t y = (bar->height + font->ascent - font->descent) / 2;
    if (P->date)
        draw_text(P->date->s, x, y, foreground, background, &barcolors.fg_status, &barcolors.bg_status,
                  bar->width, bar->height, bar->textpadding );
    return TEXT_WIDTH( "XX:XX", -1, bar->textpadding );
}

static void clockwidget_view( Bar* bar, int32_t x ) {
    awl_plugin_data_t* P = awl_plugin_data();
    if (!P) return;

    (void)bar;

    if (x < 0) calendar_hide( P->cal );
    else calendar_show( P->cal );
}

static void clockwidget_scroll( Bar* bar, uint32_t pointer_x, int amount ) {
    awl_plugin_data_t* P = awl_plugin_data();
    if (!P) return;
    (void)bar;
    (void)pointer_x;
    calendar_next( P->cal, amount );
}


static void clockwidget_click( Bar* bar, uint32_t pointer_x, int button ) {
    awl_plugin_data_t* P = awl_plugin_data();
    if (!P) return;

    (void)bar;
    (void)pointer_x;

    switch (button) {
        case BTN_LEFT:
            calendar_next( P->cal, -1 ); break;
        case BTN_RIGHT:
            calendar_next( P->cal, 1 ); break;
        case BTN_MIDDLE:
        default: break;
    }
}

static uint32_t pulsewidget_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background ) {
    awl_plugin_data_t* P = awl_plugin_data();
    if (!P) return 0;
    if (!P->pulse) return 0;
    uint32_t y = (bar->height + font->ascent - font->descent) / 2;
    char string[8];
    pixman_color_t _molokai_red = color_8bit_to_16bit(molokai_red),
                   _molokai_orange = color_8bit_to_16bit(molokai_orange);
    float val = atomic_load( &P->pulse->value );
    int muted = atomic_load( &P->pulse->muted );
    sprintf( string, "%3.0f%%", val * 100.0f );
    draw_text( string, x, y, foreground, background, muted ? &_molokai_orange :
                        lround(val*100.0) > 100 ? &_molokai_red : &barcolors.fg_status,
                        &barcolors.bg_status, bar->width, bar->height, bar->textpadding );
    return TEXT_WIDTH( "100%", -1, bar->textpadding );
}

static uint32_t ipwidget_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background ) {
    awl_plugin_data_t* P = awl_plugin_data();
    if (!P) return 0;
    if (!P->ip) return 0;
    /* if (sem_trywait( &P->ip->sem ) == -1) return 0; */

    uint32_t y = (bar->height + font->ascent - font->descent) / 2;
    pixman_color_t _molokai_red = color_8bit_to_16bit(molokai_red),
                   _molokai_green = color_8bit_to_16bit(molokai_green);
    if (*P->ip->address_string) {
        draw_text( P->ip->address_string, x, y, foreground, background,
                   P->ip->is_online ? &_molokai_green : &_molokai_red,
                   &barcolors.bg_status, bar->width, bar->height, bar->textpadding );
        return TEXT_WIDTH( P->ip->address_string, -1, bar->textpadding );
    }
    /* sem_post( &P->ip->sem ); */
    return TEXT_WIDTH( "ipaddr", -1, bar->textpadding );
}

static uint32_t tempwidget_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background ) {
    awl_plugin_data_t* P = awl_plugin_data();
    if (!P) return 0;
    if (!P->temp) return 0;
    /* if (sem_trywait( &P->temp->sem ) == -1) return 0; */

    uint32_t y = (bar->height + font->ascent - font->descent) / 2;
    uint32_t width = 0;
    for (int i=0; i<P->temp->ntemps; ++i) {
        char text[128] = {0};
        // only put the label if the string is set
        if (*P->temp->f_labels[P->temp->idx[i]])
            snprintf( text, 127, "%s:%.0f°C", P->temp->f_labels[P->temp->idx[i]], P->temp->temps[i] );
        else
            snprintf( text, 127, "%.0f°C", P->temp->temps[i] );
        pixman_color_t fgcolor = color_8bit_to_16bit( temp_color( P->temp->temps[i],
                    P->temp->f_t_min[P->temp->idx[i]], P->temp->f_t_max[P->temp->idx[i]] ) );
        draw_text( text, x, y, foreground, background, &fgcolor, &barcolors.bg_status,
                   bar->width, bar->height, bar->textpadding );
        uint32_t w = TEXT_WIDTH(text, -1, bar->textpadding);
        width += w;
        x += w;
    }
    /* sem_post( &P->temp->sem ); */
    return width;
}

#ifndef AWL_SKIP_BATWIDGET
static uint32_t batwidget_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background ) {
    awl_plugin_data_t* P = awl_plugin_data();
    if (!P) return 0;
    if (!P->bat) return 0;
    if (sem_trywait( &P->bat->sem ) == -1) return 0;

    uint32_t y = (bar->height + font->ascent - font->descent) / 2;
    if (P->bat->charging < 0) return 0;

    char text[16] = {0};
    snprintf( text, 15, "%3.0f%%", P->bat->charge * 100.0 );

    pixman_color_t fgcolor = barcolors.fg_status;
    if (P->bat->charge < 0.3)   fgcolor = color_8bit_to_16bit( molokai_orange );
    if (P->bat->charge < 0.15)  fgcolor = color_8bit_to_16bit( molokai_red );
    if (P->bat->charging)       fgcolor = color_8bit_to_16bit( molokai_green );
    draw_text( text, x, y, foreground, background, &fgcolor, &barcolors.bg_status,
               bar->width, bar->height, bar->textpadding );
    sem_post( &P->bat->sem );
    return TEXT_WIDTH( text, -1, bar->textpadding );
}
#endif

static void pulsewidget_scroll( Bar* bar, uint32_t pointer_x, int amount ) {
    (void)bar;
    (void)pointer_x;
    static char cmd[128];
    sprintf( cmd, "pactl set-sink-volume @DEFAULT_SINK@ %+d%%", -amount*2 );
    spawn_pid_str( cmd );
}

static void pulsewidget_click( Bar* bar, uint32_t pointer_x, int button ) {
    (void)bar;
    (void)pointer_x;
    switch (button) {
        case BTN_LEFT:
            spawn_pid_str("pavucontrol"); break;
        case BTN_RIGHT:
            spawn_pid_str("pulse_port_switch -t -N"); break;
        case BTN_MIDDLE:
        default:
            break;
    }
}

static uint32_t separator_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background ) {
    uint32_t y = (bar->height + font->ascent - font->descent) / 2;
    draw_text( "|", x, y, foreground, background, &barcolors.fg_status, &barcolors.bg_status, bar->width, bar->height, 0 );
    return TEXT_WIDTH( "|", -1, 0 );
}

static uint32_t systray_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background ) {
    (void)foreground;
    pixman_image_fill_boxes(PIXMAN_OP_SRC, background, &barcolors.bg_status, 1, &(pixman_box32_t){.x1=x, .y1=0, .x2=x+128, .y2=bar->height});
    return 128;
}

static uint32_t statuswidget_draw( Bar* bar, uint32_t x, pixman_image_t* fg, pixman_image_t* background ) {
    (void)fg;
    awl_plugin_data_t* P = awl_plugin_data();
    if (!P) return 0;
    if (!P->stats) return 0;
    /* if (sem_trywait( &P->stats->sem ) == -1) return 0; */

    awl_stats_t* st = P->stats;
    uint32_t widget_width = st->ncpu + st->nmem + st->nswp;
    const int ncpu = st->ncpu,
              nmem = st->nmem,
              nswp = st->nswp;
    const float *icpu = st->cpu,
                *imem = st->mem,
                *iswp = st->swp;
    widget_boxes = (pixman_box32_t*)realloc(widget_boxes, sizeof(pixman_box32_t)*2*(ncpu+nmem+nswp));
    pixman_box32_t *b_cpu = widget_boxes;
    pixman_box32_t *b_mem = b_cpu + ncpu;
    pixman_box32_t *b_swp = b_mem + nmem;
    pixman_box32_t *b_bg = b_swp + nswp;
    pixman_box32_t *b_bg_run = b_bg;

    int xx=x;
    if (icpu) {
        for (int i=0; i<ncpu; ++i) {
            int ydiv = bar->height - icpu[st->dir ? ncpu-i : i] * bar->height;
            *b_bg_run++ = (pixman_box32_t){.x1=xx,.x2=xx+1,.y1=0, .y2=ydiv};
            b_cpu[i] = (pixman_box32_t){.x1=xx,.x2=xx+1,.y1=ydiv,.y2=bar->height};
            xx++;
        }
    }
    if (imem) {
        for (int i=0; i<nmem; ++i) {
            int ydiv = bar->height - imem[st->dir ? ncpu-i : i] * bar->height;
            *b_bg_run++ = (pixman_box32_t){.x1=xx,.x2=xx+1,.y1=0, .y2=ydiv};
            b_mem[i] = (pixman_box32_t){.x1=xx,.x2=xx+1,.y1=ydiv,.y2=bar->height};
            xx++;
        }
    }
    if (iswp) {
        for (int i=0; i<nswp; ++i) {
            int ydiv = bar->height - iswp[st->dir ? ncpu-i : i] * bar->height;
            *b_bg_run++ = (pixman_box32_t){.x1=xx,.x2=xx+1,.y1=0, .y2=ydiv};
            b_swp[i] = (pixman_box32_t){.x1=xx,.x2=xx+1,.y1=ydiv,.y2=bar->height};
            xx++;
        }
    }
    pixman_image_fill_boxes(PIXMAN_OP_SRC, background, &barcolors.bg_stats, b_bg_run-b_bg, b_bg);
    pixman_image_fill_boxes(PIXMAN_OP_SRC, background, &barcolors.fg_stats_cpu, ncpu, b_cpu);
    pixman_image_fill_boxes(PIXMAN_OP_SRC, background, &barcolors.fg_stats_mem, nmem, b_mem);
    pixman_image_fill_boxes(PIXMAN_OP_SRC, background, &barcolors.fg_stats_swp, nswp, b_swp);
    /* sem_post( &st->sem ); */

    return widget_width;
}

static void statuswidget_click( Bar* bar, uint32_t pointer_x, int button ) {
    (void)bar;
    (void)pointer_x;
    (void)button;
    spawn_pid_str( "kitty -e btop" );
}

static uint32_t taskbarwidget_draw( Bar* bar, uint32_t x, pixman_image_t* foreground, pixman_image_t* background ) {
    uint32_t xspace = bar->center_widget_space;
    uint32_t nx;
    uint32_t y = (bar->height + font->ascent - font->descent) / 2;
    window_array_to_list( bar, 0 );
    if (bar->n_window_list > 0) {
        uint32_t space_per_window = xspace / bar->n_window_list;
        uint32_t pad = xspace - space_per_window*bar->n_window_list;

        x += pad/2 + pad%2;
        for (awl_title_t* T = bar->window_list; T != NULL; T = T->hh.next) {
            nx = x + space_per_window;

            // find the right bg color (maybe do that with fg too?)
            pixman_color_t bg = barcolors.bg_win;
            if (T->focused) bg = alpha_blend_16( bg, barcolors.bg_win_act );
            if (T->urgent) bg = alpha_blend_16( bg, barcolors.bg_win_urg );
            if (!T->visible) bg = alpha_blend_16( bg, barcolors.bg_win_min );

            x = draw_text( " ", x, y, foreground, background, &barcolors.fg_win, &bg, nx, bar->height, 0 );

            char* add = NULL;

            enum {
                idx_maximized,
                idx_floating,
                idx_ontop,
                NUM_IDXS
            };
            /* char symbols[NUM_IDXS][8] = { "+", "✈", "↑" }; */
            char symbols[NUM_IDXS][8] = { "M", "F", "T" };
            unsigned char sel[NUM_IDXS] = {0};
            sel[idx_floating] = T->floating;
            sel[idx_maximized] = T->maximized;
            sel[idx_ontop] = T->ontop;

            add = calloc(1,sizeof(symbols) + 3);
            strcat(add, "[");
            for (int i=0; i<NUM_IDXS; ++i) if (sel[i]) strcat(add, symbols[i]);
            strcat(add, "] ");
            if (!strcmp(add, "[] "))
                free(add), add = NULL;

            if (add) {
                x = draw_text( add, x, y, foreground, background, &barcolors.fg_win, &bg,
                        nx, bar->height, 0 );
                free(add);
            }
            x = draw_text( T->name, x, y, foreground, background, &barcolors.fg_win, &bg,
                nx, bar->height, 0 );
            pixman_image_fill_boxes(PIXMAN_OP_SRC, background, &bg, 1,
                        &(pixman_box32_t){ .x1 = x, .x2 = nx, .y1 = 0, .y2 = bar->height });
            x = nx;
        }
        x += pad/2;
        nx = x;
        HASH_CLEAR(hh, bar->window_list);
    } else {
        pixman_image_fill_boxes(PIXMAN_OP_SRC, background, &barcolors.bg_lay, 1,
                &(pixman_box32_t){ .x1 = x, .x2 = x+xspace, .y1 = 0, .y2 = bar->height });
        x += xspace;
        nx = x;
    }
    return 0;
}

static void taskbarwidget_scroll( Bar* bar, uint32_t pointer_x, int amount ) {
    awl_plugin_data_t* P = awl_plugin_data();
    if (!P) return;

    (void)bar;
    (void)pointer_x;
    if (P->focusstack) {
        (*P->focusstack)( &( (const Arg){.i=amount} ) );
        bar->redraw = true;
    }
}

static void taskbarwidget_click( Bar* bar, uint32_t pointer_x, int button ) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    uint32_t xspace = bar->center_widget_space;
    uint32_t x = 0;
    Client* c = NULL;
    int focused = 0;
    window_array_to_list( bar, 1 );
    if (bar->cpy_n_window_list > 0) {
        uint32_t space_per_window = xspace / bar->cpy_n_window_list;
        uint32_t pad = xspace - space_per_window*bar->cpy_n_window_list;
        x += pad/2 + pad%2;
        for (awl_title_t* T = bar->cpy_window_list; T != NULL; T = T->hh.next) {
            if (pointer_x >= x/buffer_scale && pointer_x <= (x+space_per_window)/buffer_scale) {
                c = T->c;
                focused = T->focused;
                goto taskbarwidget_click_return;
            }
            x += space_per_window;
        }
    }
taskbarwidget_click_return:
    HASH_CLEAR(hh, bar->cpy_window_list);
    if (c && button) {
        bar->redraw = 1;

        if (!c->visible) {
            c->visible = !c->visible;
            B->focusclient(c, 1);
        } else {
            if (focused) {
                c->visible = 0;
                B->focusclient( B->focustop( B->selmon ), 1 );
            } else {
                B->focusclient( c, 1 );
            }
        }
        B->arrange(B->selmon);
        B->printstatus();
    }
    return;
}
