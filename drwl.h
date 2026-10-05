/*
 * drwl - https://codeberg.org/sewn/drwl
 * See LICENSE.drwl file for copyright and license details.
 */
#pragma once

#include "dwl.h"

#include <stdlib.h>
#include <fcft/fcft.h>
#include <pixman-1/pixman.h>

#define BETWEEN(X, A, B) ((A) <= (X) && (X) <= (B))
#define NTAGS 9


typedef struct widget_t widget_t;
typedef struct Drwl Drwl;
typedef struct Client Client;

struct widget_t {
    uint32_t width;
    /* Right-hand widgets are laid out from the bar's right edge, so their
     * width has to be known before anything is drawn: measure() takes a
     * snapshot of whatever the widget shows and returns its exact width, then
     * draw() renders that snapshot at x into exactly that width (its return
     * value is ignored). Left and center widgets only have draw(), which
     * returns the width it used. */
    uint32_t (*measure)(widget_t* this);
    uint32_t (*draw)(widget_t* this, uint32_t x, pixman_image_t* pix);
    void (*callback_view)(widget_t* this, int32_t x_rel);
    void (*callback_click)(widget_t* this, uint32_t x_rel, int button);
    void (*callback_scroll)(widget_t* this, uint32_t x_rel, int amount);
    /* called once the pointer has rested on the widget for hover_delay_ms;
     * leaving it before then cancels, and it fires again only after the
     * pointer has left and come back */
    void (*callback_hover)(widget_t* this);
    uint32_t hover_delay_ms;
    /* called leave_delay_ms after the pointer has left a widget whose
     * callback_hover fired, unless it came back first (0: right away). While the pointer is
     * inside a layer surface whose namespace starts with popup_namespace
     * (the popup the hover opened), it counts as still on the widget. A
     * click on the widget cancels it, so a popup opened by click stays. */
    void (*callback_leave)(widget_t* this);
    uint32_t leave_delay_ms;
    const char* popup_namespace;
    double scroll_amount;
    void* userdata;
    void (*free)( void* userdata );
    Drwl* bar;
};

typedef struct drwl_window_t {
    char name[255];
    struct { uint8_t
        floating:1,
        urgent:1,
        focused:1,
        visible:1,
        maximized:1,
        fullscreen:1,
        ontop:1;
    };
    Client* c;
} drwl_window_t;

struct Drwl {
    pixman_image_t *pix;
    struct fcft_font *font;

    widget_t widgets_left[32];
    int n_widgets_left;
    widget_t widgets_right[32];
    int n_widgets_right;
    widget_t center_widget;
    uint32_t center_widget_space;
    uint32_t center_widget_start;
    int has_center_widget;

    drwl_window_t tagwindows[128];
    int n_tagwindows;
    uint32_t occ, urg, sel;
    int ntags;

    Monitor* m;
};

#define UTF_INVALID 0xFFFD
#define UTF_SIZ     4

static const unsigned char utfbyte[UTF_SIZ + 1] = {0x80,    0, 0xC0, 0xE0, 0xF0};
static const unsigned char utfmask[UTF_SIZ + 1] = {0xC0, 0x80, 0xE0, 0xF0, 0xF8};
static const uint32_t utfmin[UTF_SIZ + 1] = {       0,    0,  0x80,  0x800,  0x10000};
static const uint32_t utfmax[UTF_SIZ + 1] = {0x10FFFF, 0x7F, 0x7FF, 0xFFFF, 0x10FFFF};

static inline uint32_t
utf8decodebyte(const char c, size_t *i)
{
    for (*i = 0; *i < (UTF_SIZ + 1); ++(*i))
        if (((unsigned char)c & utfmask[*i]) == utfbyte[*i])
            return (unsigned char)c & ~utfmask[*i];
    return 0;
}

static inline size_t
utf8decode(const char *c, uint32_t *u)
{
    size_t i, j, len, type;
    uint32_t udecoded;

    *u = UTF_INVALID;
    udecoded = utf8decodebyte(c[0], &len);
    if (!BETWEEN(len, 1, UTF_SIZ))
        return 1;
    for (i = 1, j = 1; i < UTF_SIZ && j < len; ++i, ++j) {
        udecoded = (udecoded << 6) | utf8decodebyte(c[i], &type);
        if (type)
            return j;
    }
    if (j < len)
        return 0;
    *u = udecoded;
    if (!BETWEEN(*u, utfmin[len], utfmax[len]) || BETWEEN(*u, 0xD800, 0xDFFF))
        *u = UTF_INVALID;
    for (i = 1; *u > utfmax[i]; ++i)
        ;
    return len;
}

static inline pixman_color_t
convert_color(uint32_t clr)
{
    return (pixman_color_t){
        ((clr >> 24) & 0xFF) * 0x101,
        ((clr >> 16) & 0xFF) * 0x101,
        ((clr >> 8) & 0xFF) * 0x101,
        (clr & 0xFF) * 0x101
    };
}

static inline int
drwl_stride(unsigned int width)
{
    return (((PIXMAN_FORMAT_BPP(PIXMAN_a8r8g8b8) * width + 7) / 8 + 4 - 1) & -4);
}

int drwl_init(void);
Drwl * drwl_create(Monitor* m);
struct fcft_font * drwl_load_font(Drwl *drwl, size_t fontcount,
        const char *fonts[static fontcount], const char *attributes);
void drwl_destroy_font(struct fcft_font *font);
void drwl_prepare_drawing(Drwl *drwl, unsigned int w, unsigned int h, uint32_t *bits, int stride);
int drwl_text_color2(Drwl *drwl, int x, int y, unsigned int w, unsigned int h,
        unsigned int lpad, const char *text, pixman_color_t fg, pixman_color_t bg);
unsigned int drwl_font_getwidth(Drwl *drwl, const char *text);
void drwl_finish_drawing(Drwl *drwl);
void drwl_destroy(Drwl *drwl);
/* Frees every widget's userdata and empties the widget lists, so nothing in
 * the bar points into libawlplugins.so anymore. */
void drwl_widgets_clear(Drwl *drwl);
void drwl_fini(void);
