#define _GNU_SOURCE
#include "init.h"
#include "bar.h"
#include "../awl_title.h"
#include <sys/eventfd.h>

static bool has_init = false;
static int redraw_fd = -1;

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

char* awlb_date_txt = NULL;
pulse_test_t* awlb_pulse_info = NULL;
float *awlb_cpu_info = NULL, *awlb_mem_info = NULL, *awlb_swp_info = NULL;
int awlb_cpu_len = 128, awlb_mem_len = 128, awlb_swp_len = 128;
int awlb_direction = 1;

bool hidden = false;
bool bottom = true;
uint32_t vertical_padding = 2;
bool center_title = false;
bool custom_title = false;
uint32_t buffer_scale = 2;
char *fontstr;
char **tags_names;
uint32_t n_tags_names = 9;

static char fontstr_priv[] = "monospace:size=10";
static char *tags_names_priv[] = { "1", "2", "3", "4", "5", "6", "7", "✉ 8", "✉ 9" };
static pixman_box32_t* widget_boxes = NULL;

pixman_color_t bg_color_tags = COLOR_18BIT_QUICK( 22, 22, 22, FF ),
               bg_color_tags_occ = COLOR_18BIT_QUICK( 22, 22, 55, FF ),
               bg_color_tags_act = COLOR_18BIT_QUICK( 22, 33, 77, FF ),
               bg_color_tags_urg = COLOR_18BIT_QUICK( 77, 33, 22, FF ),
               fg_color_tags = COLOR_18BIT_QUICK( EE, EE, FF, FF ),

               bg_color_lay = COLOR_18BIT_QUICK( 11, 11, 11, FF ),
               fg_color_lay = COLOR_18BIT_QUICK( FF, EE, EE, FF ),

               bg_color_status = COLOR_18BIT_QUICK( 11, 11, 11, FF ),
               fg_color_status = COLOR_18BIT_QUICK( FF, EE, EE, FF ),

               bg_color_win = COLOR_18BIT_QUICK( 22, 22, 22, FF ),
               bg_color_win_min = COLOR_18BIT_QUICK( 11, 11, 11, FF ),
               bg_color_win_act = COLOR_18BIT_QUICK( 22, 22, 55, FF ),
               bg_color_win_urg = COLOR_18BIT_QUICK( 55, 22, 22, FF ),
               fg_color_win = COLOR_18BIT_QUICK( EE, EE, FF, FF ),

               bg_color_stats = COLOR_18BIT_QUICK( 11, 11, 11, FF ),
               fg_color_stats_cpu = COLOR_18BIT_QUICK( 55, 11, 11, FF ),
               fg_color_stats_mem = COLOR_18BIT_QUICK( 11, 55, 11, FF ),
               fg_color_stats_swp = COLOR_18BIT_QUICK( 11, 11, 55, FF );

#include "utf8.h"
#include "xdg-shell-protocol.h"
#include "xdg-output-unstable-v1-protocol.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"
#include "awl-ipc-unstable-v2-protocol.h"

#define _ERROR(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__);
#define ERROR(fmt, ...) _ERROR(fmt "(" __FILE__ ":%i): %s", ##__VA_ARGS__, __LINE__, strerror(errno));

#define MIN(a, b)               \
    ((a) < (b) ? (a) : (b))
#define MAX(a, b)               \
    ((a) > (b) ? (a) : (b))
#define LENGTH(x)               \
    (sizeof x / sizeof x[0])

#define ARRAY_INIT_CAP 16
#define ARRAY_EXPAND(arr, len, cap, inc)                \
    do {                                \
        uint32_t new_len, new_cap;              \
        new_len = (len) + (inc);                \
        if (new_len > (cap)) {                  \
            new_cap = new_len * 2;              \
            if (new_cap < ARRAY_INIT_CAP)           \
                new_cap = ARRAY_INIT_CAP;       \
            (arr) = realloc((arr), sizeof(*(arr)) * new_cap); \
            (cap) = new_cap;                \
        }                           \
        (len) = new_len;                    \
    } while (0)
#define ARRAY_APPEND(arr, len, cap, ptr)        \
    do {                        \
        ARRAY_EXPAND((arr), (len), (cap), 1);   \
        (ptr) = &(arr)[(len) - 1];      \
    } while (0)

#define TEXT_MAX 2048

bool awlb_run_display = false;

typedef struct {
    pixman_color_t color;
    bool bg;
    char *start;
} Color;

typedef struct {
    uint32_t btn;
    uint32_t x1;
    uint32_t x2;
    char command[128];
} Button;

typedef struct {
    char text[TEXT_MAX];
    Color *colors;
    uint32_t colors_l, colors_c;
    Button *buttons;
    uint32_t buttons_l, buttons_c;
} CustomText;

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
    int n_window_titles;
    char* layout;
    uint32_t layout_idx, last_layout_idx;
    CustomText title, status;

    bool hidden, bottom;
    bool redraw;

    struct wl_list link;
};

typedef struct {
    struct wl_seat *wl_seat;
    struct wl_pointer *wl_pointer;
    uint32_t registry_name;
    struct pointer_event pointer_event;

    Bar *bar;
    uint32_t pointer_x, pointer_y;
    uint32_t pointer_button;

    struct wl_list link;
} Seat;

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct zxdg_output_manager_v1 *output_manager;

static struct zdwl_ipc_manager_v2 *dwl_wm;
static struct wl_cursor_image *cursor_image;
static struct wl_surface *cursor_surface;

struct wl_list bar_list = {0};
struct wl_list seat_list = {0};

static char **tags;
static uint32_t tags_l, tags_c;
static char **layouts;
static uint32_t layouts_l, layouts_c;

static struct fcft_font *font;
static uint32_t height, textpadding;

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

static uint32_t draw_text(char *text,
      uint32_t x,
      uint32_t y,
      pixman_image_t *foreground,
      pixman_image_t *background,
      pixman_color_t *fg_color,
      pixman_color_t *bg_color,
      uint32_t max_x,
      uint32_t buf_height,
      uint32_t padding,
      Color *colors,
      uint32_t colors_l) {
    if (!text || !*text || !max_x)
        return x;

    uint32_t ix = x, nx;

    if ((nx = x + padding) + padding >= max_x)
        return x;
    x = nx;

    bool draw_fg = foreground && fg_color;
    bool draw_bg = background && bg_color;

    pixman_image_t *fg_fill = NULL;
    pixman_color_t *cur_bg_color;
    if (draw_fg)
        fg_fill = pixman_image_create_solid_fill(fg_color);
    if (draw_bg)
        cur_bg_color = bg_color;

    uint32_t color_ind = 0, codepoint, state = UTF8_ACCEPT, last_cp = 0;
    for (char *p = text; *p; p++) {
        /* Check for new colors */
        if (state == UTF8_ACCEPT && colors && (draw_fg || draw_bg)) {
            while (color_ind < colors_l && p == colors[color_ind].start) {
                if (colors[color_ind].bg) {
                    if (draw_bg)
                        cur_bg_color = &colors[color_ind].color;
                } else if (draw_fg) {
                    pixman_image_unref(fg_fill);
                    fg_fill = pixman_image_create_solid_fill(&colors[color_ind].color);
                }
                color_ind++;
            }
        }

        /* Returns nonzero if more bytes are needed */
        if (utf8decode(&state, &codepoint, *p))
            continue;

        /* Turn off subpixel rendering, which complicates things when
         * mixed with alpha channels */
        const struct fcft_glyph *glyph = fcft_rasterize_char_utf32(font, codepoint, FCFT_SUBPIXEL_NONE);
        if (!glyph)
            continue;

        /* Adjust x position based on kerning with previous glyph */
        long kern = 0;
        if (last_cp)
            fcft_kerning(font, last_cp, codepoint, &kern, NULL);
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

#define TEXT_WIDTH(text, maxwidth, padding)             \
    draw_text(text, 0, 0, NULL, NULL, NULL, NULL, maxwidth, 0, padding, NULL, 0)

static int draw_frame(Bar *bar) {
    /* Allocate buffer to be attached to the surface */
        int fd = allocate_shm_file(bar->bufsize);
    if (fd == -1)
        return -1;

    uint32_t *data = mmap(NULL, bar->bufsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return -1;
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

    // setup drawing coordinates
    uint32_t x = 0;
    uint32_t y = (bar->height + font->ascent - font->descent) / 2;
    uint32_t boxs = font->height / 9;
    uint32_t boxw = font->height / 6 + 2;

    // draw tags
    for (uint32_t i = 0; i < tags_l; i++) {
        bool active = bar->mtags & 1 << i;
        bool occupied = bar->ctags & 1 << i;
        bool urgent = bar->urg & 1 << i;

        // draw small box
        if (occupied) {
            pixman_image_fill_boxes(PIXMAN_OP_SRC, foreground,
                        &fg_color_tags, 1, &(pixman_box32_t){
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
        pixman_color_t bg = bg_color_tags;
        if (occupied) bg = alpha_blend_16( bg, bg_color_tags_occ );
        if (active) bg = alpha_blend_16( bg, bg_color_tags_act );
        if (urgent) bg = alpha_blend_16( bg, bg_color_tags_urg );

        x = draw_text(tags[i], x, y, foreground, background, &fg_color_tags, &bg,
                bar->width, bar->height, bar->textpadding, NULL, 0);
    }

    x = draw_text(bar->layout, x, y, foreground, background,
              &fg_color_lay, &bg_color_lay, bar->width,
              bar->height, bar->textpadding, NULL, 0);

    uint32_t status_width = 0;
    char status_txt[512] = {0};
    if (awlb_date_txt)
        strcat( status_txt, awlb_date_txt );
    if (awlb_pulse_info)
        sprintf( status_txt + strlen(status_txt), " | %.0f%%", awlb_pulse_info->value * 100.0f );
    uint32_t widget_width = 0;
    if (awlb_cpu_info) widget_width += awlb_cpu_len;
    if (awlb_mem_info) widget_width += awlb_mem_len;
    if (awlb_swp_info) widget_width += awlb_swp_len;
    status_width = TEXT_WIDTH(status_txt, bar->width - x, bar->textpadding) + widget_width;
    draw_text(status_txt, bar->width - status_width + widget_width, y, foreground,
              background, &fg_color_status, &bg_color_status,
              bar->width, bar->height, bar->textpadding,
              bar->status.colors, bar->status.colors_l);
    int xx = bar->width - status_width;

    const int ncpu = awlb_cpu_len,
              nmem = awlb_mem_len,
              nswp = awlb_swp_len;
    const float *icpu = awlb_cpu_info,
                *imem = awlb_mem_info,
                *iswp = awlb_swp_info;
    widget_boxes = (pixman_box32_t*)realloc(widget_boxes, sizeof(pixman_box32_t)*2*(ncpu+nmem+nswp));

    pixman_box32_t *b_cpu = widget_boxes;
    pixman_box32_t *b_mem = b_cpu + ncpu;
    pixman_box32_t *b_swp = b_mem + nmem;
    pixman_box32_t *b_bg = b_swp + nswp;
    pixman_box32_t *b_bg_run = b_bg;

    if (icpu) {
        for (int i=0; i<ncpu; ++i) {
            int ydiv = bar->height - icpu[awlb_direction ? ncpu-i : i] * bar->height;
            *b_bg_run++ = (pixman_box32_t){.x1=xx,.x2=xx+1,.y1=0, .y2=ydiv};
            b_cpu[i] = (pixman_box32_t){.x1=xx,.x2=xx+1,.y1=ydiv,.y2=bar->height};
            xx++;
        }
    }
    if (imem) {
        for (int i=0; i<nmem; ++i) {
            int ydiv = bar->height - imem[awlb_direction ? ncpu-i : i] * bar->height;
            *b_bg_run++ = (pixman_box32_t){.x1=xx,.x2=xx+1,.y1=0, .y2=ydiv};
            b_mem[i] = (pixman_box32_t){.x1=xx,.x2=xx+1,.y1=ydiv,.y2=bar->height};
            xx++;
        }
    }
    if (iswp) {
        for (int i=0; i<nswp; ++i) {
            int ydiv = bar->height - iswp[awlb_direction ? ncpu-i : i] * bar->height;
            *b_bg_run++ = (pixman_box32_t){.x1=xx,.x2=xx+1,.y1=0, .y2=ydiv};
            b_swp[i] = (pixman_box32_t){.x1=xx,.x2=xx+1,.y1=ydiv,.y2=bar->height};
            xx++;
        }
    }
    pixman_image_fill_boxes(PIXMAN_OP_SRC, background, &bg_color_stats, b_bg_run-b_bg, b_bg);
    pixman_image_fill_boxes(PIXMAN_OP_SRC, background, &fg_color_stats_cpu, ncpu, b_cpu);
    pixman_image_fill_boxes(PIXMAN_OP_SRC, background, &fg_color_stats_mem, nmem, b_mem);
    pixman_image_fill_boxes(PIXMAN_OP_SRC, background, &fg_color_stats_swp, nswp, b_swp);
    // status text
    /* uint32_t status_width = TEXT_WIDTH(bar->status.text, bar->width - x, bar->textpadding); */
    /* draw_text(bar->status.text, bar->width - status_width, y, foreground, */
    /*       background, &fg_color_status, &bg_color_status, */
    /*       bar->width, bar->height, bar->textpadding, */
    /*       bar->status.colors, bar->status.colors_l); */

    uint32_t nx;

    uint32_t xspace = bar->width - status_width - x;
    if (bar->n_window_titles > 0) {
        uint32_t space_per_window = xspace / bar->n_window_titles;
        uint32_t pad = xspace - space_per_window*bar->n_window_titles;

        x += pad/2 + pad%2;
        for (int i=0; i<bar->n_window_titles; ++i) {
            awl_title_t* T = bar->window_titles + i;
            nx = x + space_per_window;

            // find the right bg color (maybe do that with fg too?)
            pixman_color_t bg = bg_color_win;
            if (T->focused) bg = alpha_blend_16( bg, bg_color_win_act );
            if (T->urgent) bg = alpha_blend_16( bg, bg_color_win_urg );
            if (!T->visible) bg = alpha_blend_16( bg, bg_color_win_min );

            // TODO T->floating
            x = draw_text( T->name, x, y, foreground, background, &fg_color_win, &bg,
                nx, bar->height, 0, NULL, 0 );
            pixman_image_fill_boxes(PIXMAN_OP_SRC, background, &bg, 1,
                        &(pixman_box32_t){ .x1 = x, .x2 = nx, .y1 = 0, .y2 = bar->height });
            x = nx;
        }
        x += pad/2;
        nx = x;
    } else {
        pixman_image_fill_boxes(PIXMAN_OP_SRC, background, &bg_color_lay, 1,
                &(pixman_box32_t){ .x1 = x, .x2 = x+xspace, .y1 = 0, .y2 = bar->height });
        x += xspace;
        nx = x;
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

    return 0;
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
    (void)data;
    (void)surface;
    awlb_run_display = false;
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

static void shell_command(char *command) {
    if (fork() == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", command, NULL);
        exit(EXIT_SUCCESS);
    }
}

static void pointer_enter(void *data, struct wl_pointer *pointer,
          uint32_t serial, struct wl_surface *surface,
          wl_fixed_t surface_x, wl_fixed_t surface_y) {
    (void)surface_x;
    (void)surface_y;
    Seat *seat = (Seat *)data;

    seat->bar = NULL;
    Bar *bar;
    wl_list_for_each(bar, &bar_list, link) {
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

    seat->pointer_x = wl_fixed_to_int(surface_x);
    seat->pointer_y = wl_fixed_to_int(surface_y);
}

static void pointer_frame(void *data, struct wl_pointer *pointer) {
    (void)pointer;
    Seat *seat = (Seat *)data;

    if (!seat->bar) return;

    int discrete_event = 0;
    struct pointer_event* event = &seat->pointer_event;
    if (event->event_mask & POINTER_EVENT_AXIS_DISCRETE &&
        event->axes[WL_POINTER_AXIS_VERTICAL_SCROLL].valid) {
        discrete_event = event->axes[WL_POINTER_AXIS_VERTICAL_SCROLL].discrete;
    }
    memset(event, 0, sizeof(*event));

    uint32_t x = 0, i = 0;
    do {
        x += TEXT_WIDTH(tags[i], seat->bar->width - x, seat->bar->textpadding) / buffer_scale;
    } while (seat->pointer_x >= x && ++i < tags_l);

    // scroll events!
    if (discrete_event) {
        seat->bar->redraw = true;
        if (i < tags_l) {
            cycle_tag( &( (const Arg){.i=discrete_event} ) );
            return;
        } else if ((seat->pointer_x <
                    (x + TEXT_WIDTH(seat->bar->layout, seat->bar->width - x, seat->bar->textpadding)))) {
            cycle_layout( &( (const Arg){.i=discrete_event} ) );
            return;
        } else {
            focusstack( &( (const Arg){.i=discrete_event} ) );
            return;
        }
    }

    if (!seat->pointer_button) return;

    if (i < tags_l) {
        if (seat->pointer_button == BTN_LEFT)
            view( &( (const Arg){.ui=1<<i} ) );
        else if (seat->pointer_button == BTN_MIDDLE)
            view( &( (const Arg){.ui=~0} ) );
        else if (seat->pointer_button == BTN_RIGHT)
            toggleview( &( (const Arg){.ui=1<<i} ) );
    } else if (seat->pointer_x < (x += TEXT_WIDTH(seat->bar->layout, seat->bar->width - x, seat->bar->textpadding))) {
        if (seat->pointer_button == BTN_LEFT)
            cycle_layout( &( (const Arg){.i=-1} ) );
        else if (seat->pointer_button == BTN_RIGHT)
            cycle_layout( &( (const Arg){.i=+1} ) );
    } else {
        uint32_t status_x = seat->bar->width / buffer_scale - TEXT_WIDTH(seat->bar->status.text, seat->bar->width - x, seat->bar->textpadding) / buffer_scale;
        if (seat->pointer_x < status_x) {
            /* Clicked on title */
            if (custom_title) {
                if (center_title) {
                    uint32_t title_width = TEXT_WIDTH(seat->bar->title.text, status_x - x, 0);
                    x = MAX(x, MIN((seat->bar->width - title_width) / 2, status_x - title_width));
                } else {
                    x = MIN(x + seat->bar->textpadding, status_x);
                }
                for (i = 0; i < seat->bar->title.buttons_l; i++) {
                    if (seat->pointer_button == seat->bar->title.buttons[i].btn
                        && seat->pointer_x >= x + seat->bar->title.buttons[i].x1
                        && seat->pointer_x < x + seat->bar->title.buttons[i].x2) {
                        shell_command(seat->bar->title.buttons[i].command);
                        break;
                    }
                }
            }
        } else {
            /* Clicked on status */
            for (i = 0; i < seat->bar->status.buttons_l; i++) {
                if (seat->pointer_button == seat->bar->status.buttons[i].btn
                    && seat->pointer_x >= status_x + seat->bar->textpadding + seat->bar->status.buttons[i].x1 / buffer_scale
                    && seat->pointer_x < status_x + seat->bar->textpadding + seat->bar->status.buttons[i].x2 / buffer_scale) {
                    shell_command(seat->bar->status.buttons[i].command);
                    break;
                }
            }
        }
    }

    seat->pointer_button = 0;
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
        ERROR("Could not create wl_surface");

    bar->layer_surface = zwlr_layer_shell_v1_get_layer_surface(layer_shell, bar->wl_surface, bar->wl_output,
                                   ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM, "awl_bar");
    if (!bar->layer_surface)
        ERROR("Could not create layer_surface");
    zwlr_layer_surface_v1_add_listener(bar->layer_surface, &layer_surface_listener, bar);

    zwlr_layer_surface_v1_set_size(bar->layer_surface, 0, bar->height / buffer_scale);
    zwlr_layer_surface_v1_set_anchor(bar->layer_surface,
                     (bar->bottom ? ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM : ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)
                     | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
                     | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
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

static void dwl_wm_tags(void *data, struct zdwl_ipc_manager_v2 *dwl_wm,
    uint32_t amount) {
    (void)data;
    (void)dwl_wm;
    if (!tags)
        tags = malloc(amount * sizeof(char *));
    uint32_t i = tags_l;
    ARRAY_EXPAND(tags, tags_l, tags_c, MAX(0, (int)amount - (int)tags_l));
    for (; i < amount; i++)
        tags[i] = strdup(tags_names[MIN(i, n_tags_names-1)]);
}

static void dwl_wm_layout(void *data, struct zdwl_ipc_manager_v2 *dwl_wm,
    const char *name) {
    (void)data;
    (void)dwl_wm;
    char **ptr;
    ARRAY_APPEND(layouts, layouts_l, layouts_c, ptr);
    *ptr = strdup(name);
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

static void dwl_wm_output_active(void *data, struct zdwl_ipc_output_v2 *dwl_wm_output,
        uint32_t active) {
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

    bar->last_layout_idx = bar->layout_idx;
    bar->layout_idx = layout;
}

static void dwl_wm_output_title_ary(void *data, struct zdwl_ipc_output_v2 *dwl_wm_output,
    struct wl_array* ary) {
    (void)dwl_wm_output;
    if (custom_title)
        return;

    Bar *bar = (Bar *)data;

    awl_title_t* titles = (awl_title_t*)ary->data;
    bar->n_window_titles = ary->size / sizeof(awl_title_t);
    bar->window_titles = realloc(bar->window_titles, ary->size);
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

    if (layouts[bar->layout_idx])
        free(layouts[bar->layout_idx]);
    layouts[bar->layout_idx] = strdup(layout);
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
    bar->height = height * buffer_scale;
    bar->textpadding = textpadding;
    bar->bottom = bottom;
    bar->hidden = hidden;
    bar->window_titles = NULL;

    bar->xdg_output = zxdg_output_manager_v1_get_xdg_output(output_manager, bar->wl_output);
    if (!bar->xdg_output)
        ERROR("Could not create xdg_output");
    zxdg_output_v1_add_listener(bar->xdg_output, &output_listener, bar);

    bar->dwl_wm_output = zdwl_ipc_manager_v2_get_output(dwl_wm, bar->wl_output);
    if (!bar->dwl_wm_output)
        ERROR("Could not create dwl_wm_output");
    zdwl_ipc_output_v2_add_listener(bar->dwl_wm_output, &dwl_wm_output_listener, bar);

    if (!bar->hidden)
        show_bar(bar);
}

static void handle_global(void *data, struct wl_registry *registry,
          uint32_t name, const char *interface, uint32_t version) {
    (void)data;
    (void)version;
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
        if (awlb_run_display)
            setup_bar(bar);
        wl_list_insert(&bar_list, &bar->link);
    } else if (!strcmp(interface, wl_seat_interface.name)) {
        Seat *seat = calloc(1, sizeof(Seat));
        seat->registry_name = name;
        seat->wl_seat = wl_registry_bind(registry, name, &wl_seat_interface, 7);
        wl_seat_add_listener(seat->wl_seat, &seat_listener, seat);
        wl_list_insert(&seat_list, &seat->link);
    }
}

static void teardown_bar(Bar *bar) {
    if (bar->status.colors)
        free(bar->status.colors);
    if (bar->status.buttons)
        free(bar->status.buttons);
    if (bar->title.colors)
        free(bar->title.colors);
    if (bar->title.buttons)
        free(bar->title.buttons);
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
    Bar *bar;
    Seat *seat;

    printf("in global remove\n");
    wl_list_for_each(bar, &bar_list, link) {
        if (bar->registry_name == name) {
            wl_list_remove(&bar->link);
            teardown_bar(bar);
            return;
        }
    }
    wl_list_for_each(seat, &seat_list, link) {
        if (seat->registry_name == name) {
            wl_list_remove(&seat->link);
            teardown_seat(seat);
            return;
        }
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove
};

static void event_loop(void) {
    int wl_fd = wl_display_get_fd(display);

    while (awlb_run_display) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(wl_fd, &rfds);
        FD_SET(redraw_fd, &rfds);

        wl_display_flush(display);

        #define MMAX(a,b) ((a) > (b) ? (a) : (b))
        if (select(MMAX(redraw_fd,wl_fd)+1, &rfds, NULL, NULL, NULL) == -1) {
            if (errno == EINTR)
                continue;
            else
                ERROR("select");
        }
        if (FD_ISSET(wl_fd, &rfds))
            if (wl_display_dispatch(display) == -1)
                break;
        // TODO is this still a bug?
        if (FD_ISSET(redraw_fd, &rfds)) {
            uint64_t val = 0;
            eventfd_read(redraw_fd, &val);
        }

        Bar *bar;
        wl_list_for_each(bar, &bar_list, link) {
            if (bar->redraw) {
                if (!bar->hidden)
                    draw_frame(bar);
                bar->redraw = false;
            }
        }
    }

}

static void cleanup_fun(void* arg) {
    (void)arg;
    if (redraw_fd != -1) close( redraw_fd );
    if (tags) {
        for (uint32_t i = 0; i < tags_l; i++)
            free(tags[i]);
        free(tags);
    }
    if (layouts) {
        for (uint32_t i = 0; i < layouts_l; i++)
            free(layouts[i]);
        free(layouts);
    }
    free( widget_boxes );

    Bar *bar, *bar2;
    Seat *seat, *seat2;
    wl_list_for_each_safe(bar, bar2, &bar_list, link)
        teardown_bar(bar);
    wl_list_for_each_safe(seat, seat2, &seat_list, link)
        teardown_seat(seat);

    zwlr_layer_shell_v1_destroy(layer_shell);
    zxdg_output_manager_v1_destroy(output_manager);
    zdwl_ipc_manager_v2_destroy(dwl_wm);

    fcft_destroy(font);
    fcft_fini();

    wl_shm_destroy(shm);
    wl_compositor_destroy(compositor);
    wl_display_disconnect(display);
}

void* awl_bar_run( void* arg ) {
    (void)arg;

    fontstr = fontstr_priv;
    tags_names = tags_names_priv;

    /* Set up display and protocols */
    display = wl_display_connect(NULL);
    if (!display)
        ERROR("Failed to create display");

    wl_list_init(&bar_list);
    wl_list_init(&seat_list);

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);
    if (!compositor || !shm || !layer_shell || !output_manager || !dwl_wm)
        ERROR("Compositor does not support all needed protocols");

    /* Load selected font */
    fcft_init(FCFT_LOG_COLORIZE_AUTO, 0, FCFT_LOG_CLASS_ERROR);
    fcft_set_scaling_filter(FCFT_SCALING_FILTER_LANCZOS3);

    unsigned int dpi = 96 * buffer_scale;
    char buf[10];
    snprintf(buf, sizeof buf, "dpi=%u", dpi);
    if (!(font = fcft_from_name(1, (const char *[]) {fontstr}, buf)))
        ERROR("Could not load font");
    textpadding = font->height / 2;
    height = font->height / buffer_scale + vertical_padding * 2;

    /* Setup bars */
    Bar* bar;
    wl_list_for_each(bar, &bar_list, link)
        setup_bar(bar);
    wl_display_roundtrip(display);

    redraw_fd = eventfd(0,0);

    pthread_cleanup_push( &cleanup_fun, NULL );
    awlb_run_display = true;
    has_init = true;
    event_loop();
    has_init = false;
    pthread_cleanup_pop( true );

    return NULL;
}

static void awl_bar_refresh_fun( void ) {
    if (!has_init) return;
    Bar* bar;
    wl_list_for_each(bar, &bar_list, link)
        bar->redraw = true;
    eventfd_write(redraw_fd, 1);
}

void* awl_bar_refresh( void* arg ) {
    float* prsec = (float*)arg;
    while (1) {
        usleep( (useconds_t)(*prsec * 1.e6) );
        awl_bar_refresh_fun();
    }
    return NULL;
}
