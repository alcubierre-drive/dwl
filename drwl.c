#include "drwl.h"
#include "plugins.h"
#include "plugins/date.h"
#include "plugins/colors.h"

int drwl_init(void) {
    fcft_set_scaling_filter(FCFT_SCALING_FILTER_LANCZOS3);
    return fcft_init(FCFT_LOG_COLORIZE_AUTO, 0, FCFT_LOG_CLASS_ERROR);
}

static uint32_t tagwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix );
static uint32_t taskbarwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix );
static uint32_t layoutwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix );
static uint32_t clockwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix );
static uint32_t systray_draw( widget_t* w, uint32_t x, pixman_image_t* pix );
static uint32_t pulsewidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix );
static uint32_t statuswidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix );
static uint32_t tempwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix );
static uint32_t batwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix );
static uint32_t ipwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix );

static void dummy_click(widget_t* this, uint32_t x_rel, int button) {
    printf( "%p got clicked! %u:%i\n", this, x_rel, button );
}
static void dummy_scroll(widget_t* this, uint32_t x_rel, int amount) {
    printf( "%p got scrolled! %u:%i\n", this, x_rel, amount );
}

Drwl * drwl_create(Monitor* m) {
    Drwl *drwl;

    if (!(drwl = calloc(1, sizeof(Drwl))))
        return NULL;
    drwl->m = m;

    drwl->widgets_left[drwl->n_widgets_left++] = (widget_t){
        .bar = drwl,
        .draw = &tagwidget_draw,
        .callback_click = &dummy_click,
        .callback_scroll = &dummy_scroll,
    };
    drwl->widgets_left[drwl->n_widgets_left++] = (widget_t){
        .bar = drwl,
        .draw = &layoutwidget_draw,
        .callback_click = &dummy_click,
        .callback_scroll = &dummy_scroll,
    };

    drwl->center_widget = (widget_t){
        .bar = drwl,
        .draw = &taskbarwidget_draw,
        .callback_click = &dummy_click,
        .callback_scroll = &dummy_scroll,
    };
    drwl->has_center_widget = 1;

    drwl->widgets_right[drwl->n_widgets_right++] = (widget_t){
        .bar = drwl,
        .draw = &clockwidget_draw,
        .callback_click = &dummy_click,
        .callback_scroll = &dummy_scroll,
    };
    drwl->widgets_right[drwl->n_widgets_right++] = (widget_t){
        .bar = drwl,
        .draw = &systray_draw,
        .width = 64,
        .callback_click = &dummy_click,
        .callback_scroll = &dummy_scroll,
    };
    drwl->widgets_right[drwl->n_widgets_right++] = (widget_t){
        .bar = drwl,
        .draw = &pulsewidget_draw,
        .callback_click = &dummy_click,
        .callback_scroll = &dummy_scroll,
    };
    drwl->widgets_right[drwl->n_widgets_right++] = (widget_t){
        .bar = drwl,
        .draw = &statuswidget_draw,
        .width = 16*3,
        .free = free,
        .callback_click = &dummy_click,
        .callback_scroll = &dummy_scroll,
    };
    drwl->widgets_right[drwl->n_widgets_right++] = (widget_t){
        .bar = drwl,
        .draw = &tempwidget_draw,
        .width = 22,
        .free = free,
        .callback_click = &dummy_click,
        .callback_scroll = &dummy_scroll,
    };
    drwl->widgets_right[drwl->n_widgets_right++] = (widget_t){
        .bar = drwl,
        .draw = &batwidget_draw,
        .width = 22,
        .callback_click = &dummy_click,
        .callback_scroll = &dummy_scroll,
    };
    drwl->widgets_right[drwl->n_widgets_right++] = (widget_t){
        .bar = drwl,
        .draw = &ipwidget_draw,
        .width = 50,
        .callback_click = &dummy_click,
        .callback_scroll = &dummy_scroll,
    };

    sem_init(&drwl->draw, 0, 0);
    sem_post(&drwl->draw);
    return drwl;
}

void drwl_setfont(Drwl *drwl, struct fcft_font *font) {
    if (drwl)
        drwl->font = font;
}

struct fcft_font * drwl_load_font(Drwl *drwl, size_t fontcount, const char *fonts[static fontcount], const char *attributes) {
    struct fcft_font *font = fcft_from_name(fontcount, fonts, attributes);
    if (drwl)
        drwl_setfont(drwl, font);
    return font;
}

void drwl_destroy_font(struct fcft_font *font) {
    fcft_destroy(font);
}

void drwl_setscheme(Drwl *drwl, uint32_t *scm) {
    if (drwl)
        drwl->scheme = scm;
}

void drwl_prepare_drawing(Drwl *drwl, unsigned int w, unsigned int h, uint32_t *bits, int stride) {
    pixman_region32_t clip;

    if (!drwl)
        return;

    drwl->pix = pixman_image_create_bits_no_clear(
        PIXMAN_a8r8g8b8, w, h, bits, stride);
    pixman_region32_init_rect(&clip, 0, 0, w, h);
    pixman_image_set_clip_region32(drwl->pix, &clip);
    pixman_region32_fini(&clip);
}

void drwl_rect(Drwl *drwl, int x, int y, unsigned int w, unsigned int h, int filled, int invert) {
    if (!drwl || !drwl->scheme || !drwl->pix)
        return;
    drwl_rect_color(drwl, x, y, w, h, filled, drwl->scheme[invert ? ColBg : ColFg]);
}

void drwl_rect_color(Drwl *drwl, int x, int y, unsigned int w, unsigned int h, int filled, uint32_t color) {
    pixman_color_t clr;
    if (!drwl || !drwl->scheme || !drwl->pix)
        return;

    clr = convert_color(color);
    if (filled)
        pixman_image_fill_rectangles(PIXMAN_OP_SRC, drwl->pix, &clr, 1,
            &(pixman_rectangle16_t){x, y, w, h});
    else
        pixman_image_fill_rectangles(PIXMAN_OP_SRC, drwl->pix, &clr, 4,
            (pixman_rectangle16_t[4]){
                { x,         y,         w, 1 },
                { x,         y + h - 1, w, 1 },
                { x,         y,         1, h },
                { x + w - 1, y,         1, h }});
}

/*int drwl_text(Drwl *drwl, int x, int y, unsigned int w, unsigned int h, unsigned int lpad,*/
/*        const char *text, int invert) {*/
/*    int render = x || y || w || h;*/
/*    if (!drwl || (render && (!drwl->scheme || !w || !drwl->pix)) || !text || !drwl->font)*/
/*        return 0;*/
/*    if (!render)*/
/*        w = invert ? invert : ~invert;*/
/*    return drwl_text_color(drwl, x, y, w, h, lpad, text, drwl->scheme[!invert?ColFg:ColBg],*/
/*            drwl->scheme[!invert?ColBg:ColFg] );*/
/*}*/

int drwl_text(Drwl *drwl, int x, int y, unsigned int w, unsigned int h, unsigned int lpad,
        const char *text, int invert) {
    int ty;
    int utf8charlen, render = x || y || w || h;
    long x_kern;
    uint32_t cp = 0, last_cp = 0;
    pixman_color_t clr;
    pixman_image_t *fg_pix = NULL;
    int noellipsis = 0;
    const struct fcft_glyph *glyph, *eg;
    int fcft_subpixel_mode = FCFT_SUBPIXEL_DEFAULT;

    if (!drwl || (render && (!drwl->scheme || !w || !drwl->pix)) || !text || !drwl->font)
        return 0;

    if (!render) {
        w = invert ? invert : ~invert;
    } else {
        clr = convert_color(drwl->scheme[invert ? ColBg : ColFg]);
        fg_pix = pixman_image_create_solid_fill(&clr);

        drwl_rect(drwl, x, y, w, h, 1, !invert);

        x += lpad;
        w -= lpad;
    }

    if (render && (drwl->scheme[ColBg] & 0xFF) != 0xFF)
        fcft_subpixel_mode = FCFT_SUBPIXEL_NONE;

    // U+2026 == …
    eg = fcft_rasterize_char_utf32(drwl->font, 0x2026, fcft_subpixel_mode);

    while (*text) {
        utf8charlen = utf8decode(text, &cp);

        glyph = fcft_rasterize_char_utf32(drwl->font, cp, fcft_subpixel_mode);
        if (!glyph)
            continue;

        x_kern = 0;
        if (last_cp)
            fcft_kerning(drwl->font, last_cp, cp, &x_kern, NULL);
        last_cp = cp;

        ty = y + (h - drwl->font->height) / 2 + drwl->font->ascent;

        /* draw ellipsis if remaining text doesn't fit */
        if (!noellipsis && x_kern + glyph->advance.x + eg->advance.x > w && *(text + 1) != '\0') {
            if (drwl_text(drwl, 0, 0, 0, 0, 0, text, 0)
                    - glyph->advance.x < eg->advance.x) {
                noellipsis = 1;
            } else {
                w -= eg->advance.x;
                pixman_image_composite32(
                    PIXMAN_OP_OVER, fg_pix, eg->pix, drwl->pix, 0, 0, 0, 0,
                    x + eg->x, ty - eg->y, eg->width, eg->height);
            }
        }

        if ((x_kern + glyph->advance.x) > w)
            break;

        x += x_kern;

        if (render && pixman_image_get_format(glyph->pix) == PIXMAN_a8r8g8b8)
            // pre-rendered glyphs (eg. emoji)
            pixman_image_composite32(
                PIXMAN_OP_OVER, glyph->pix, NULL, drwl->pix, 0, 0, 0, 0,
                x + glyph->x, ty - glyph->y, glyph->width, glyph->height);
        else if (render)
            pixman_image_composite32(
                PIXMAN_OP_OVER, fg_pix, glyph->pix, drwl->pix, 0, 0, 0, 0,
                x + glyph->x, ty - glyph->y, glyph->width, glyph->height);

        text += utf8charlen;
        x += glyph->advance.x;
        w -= glyph->advance.x;
    }

    if (render)
        pixman_image_unref(fg_pix);

    return x + (render ? w : 0);
}

int drwl_text_color2(Drwl *drwl, int x, int y, unsigned int w, unsigned int h,
        unsigned int lpad, const char *text, pixman_color_t fg, pixman_color_t bg) {
    return drwl_text_color(drwl, x, y, w, h, lpad, text, color_16bit_to_8bit(fg), color_16bit_to_8bit(bg));
}

void drwl_rect_color2(Drwl *drwl, int x, int y, unsigned int w, unsigned int h, int filled, pixman_color_t color) {
    drwl_rect_color(drwl, x, y, w, h, filled, color_16bit_to_8bit(color));
}

int drwl_text_color(Drwl *drwl, int x, int y, unsigned int w, unsigned int h,
        unsigned int lpad, const char *text, uint32_t fg, uint32_t bg) {
    int ty;
    int utf8charlen, render = x || y || w || h;
    long x_kern;
    uint32_t cp = 0, last_cp = 0;
    pixman_color_t clr;
    pixman_image_t *fg_pix = NULL;
    int noellipsis = 0;
    const struct fcft_glyph *glyph, *eg;
    int fcft_subpixel_mode = FCFT_SUBPIXEL_DEFAULT;

    if (!drwl || (render && (!drwl->scheme || !w || !drwl->pix)) || !text || !drwl->font)
        return 0;

    if (!render) {
        // w = ...;
    } else {
        clr = convert_color(fg);
        fg_pix = pixman_image_create_solid_fill(&clr);

        drwl_rect_color(drwl, x, y, w, y<0?h-y:h, 1, bg);

        x += lpad;
        w -= lpad;
    }

    if (render && (bg & 0xFF) != 0xFF)
        fcft_subpixel_mode = FCFT_SUBPIXEL_NONE;

    // U+2026 == …
    eg = fcft_rasterize_char_utf32(drwl->font, 0x2026, fcft_subpixel_mode);

    while (*text) {
        utf8charlen = utf8decode(text, &cp);

        glyph = fcft_rasterize_char_utf32(drwl->font, cp, fcft_subpixel_mode);
        if (!glyph)
            continue;

        x_kern = 0;
        if (last_cp)
            fcft_kerning(drwl->font, last_cp, cp, &x_kern, NULL);
        last_cp = cp;

        ty = y + (h - drwl->font->height) / 2 + drwl->font->ascent;

        /* draw ellipsis if remaining text doesn't fit */
        if (!noellipsis && x_kern + glyph->advance.x + eg->advance.x > w && *(text + 1) != '\0') {
            if (drwl_text(drwl, 0, 0, 0, 0, 0, text, 0)
                    - glyph->advance.x < eg->advance.x) {
                noellipsis = 1;
            } else {
                w -= eg->advance.x;
                pixman_image_composite32(
                    PIXMAN_OP_OVER, fg_pix, eg->pix, drwl->pix, 0, 0, 0, 0,
                    x + eg->x, ty - eg->y, eg->width, eg->height);
            }
        }

        if ((x_kern + glyph->advance.x) > w)
            break;

        x += x_kern;

        if (render && pixman_image_get_format(glyph->pix) == PIXMAN_a8r8g8b8)
            // pre-rendered glyphs (eg. emoji)
            pixman_image_composite32(
                PIXMAN_OP_OVER, glyph->pix, NULL, drwl->pix, 0, 0, 0, 0,
                x + glyph->x, ty - glyph->y, glyph->width, glyph->height);
        else if (render)
            pixman_image_composite32(
                PIXMAN_OP_OVER, fg_pix, glyph->pix, drwl->pix, 0, 0, 0, 0,
                x + glyph->x, ty - glyph->y, glyph->width, glyph->height);

        text += utf8charlen;
        x += glyph->advance.x;
        w -= glyph->advance.x;
    }

    if (render)
        pixman_image_unref(fg_pix);

    return x + (render ? w : 0);
}

unsigned int drwl_font_getwidth(Drwl *drwl, const char *text) {
    if (!drwl || !drwl->font || !text)
        return 0;
    return drwl_text(drwl, 0, 0, 0, 0, 0, text, 0);
}

void drwl_finish_drawing(Drwl *drwl) {
    if (drwl && drwl->pix)
        pixman_image_unref(drwl->pix);
}

void drwl_destroy(Drwl *drwl) {
    sem_wait(&drwl->draw);
    sem_destroy(&drwl->draw);
    if (drwl->pix)
        pixman_image_unref(drwl->pix);
    if (drwl->font)
        drwl_destroy_font(drwl->font);

    for (int l=0; l<drwl->n_widgets_left; ++l)
        if (drwl->widgets_left[l].free)
            drwl->widgets_left[l].free( drwl->widgets_left[l].userdata );
    for (int r=0; r<drwl->n_widgets_right; ++r)
        if (drwl->widgets_right[r].free)
            drwl->widgets_right[r].free( drwl->widgets_right[r].userdata );

    if (drwl->has_center_widget)
        if (drwl->center_widget.free)
            drwl->center_widget.free( drwl->center_widget.userdata );

    free(drwl);
}

void drwl_fini(void) {
    fcft_fini();
}

#define TEXT( width, string, fg, bg ) \
    drwl_text_color2( w->bar, x, -2, width, w->bar->m->b.height, w->bar->m->lrpad/2, string, \
            fg, bg )

static uint32_t tagwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix ) {
    awl_plugin_data_t* P = awl_plugin_get();
    if (!P) return 0;
    int width = 0;
    for (int t=0; t<w->bar->ntags; ++t) {
        char num[16] = ""; snprintf( num, sizeof(num)-1, "%i", t+1 );
        int ww = TEXTW(w->bar->m, "0");
        pixman_color_t bg_color = color_8bit_to_16bit(molokai_dark_gray);
        pixman_color_t bg_add;
        if (w->bar->occ & (1 << t)) {
            bg_add = color_8bit_to_16bit(molokai_orange);
            bg_add.alpha = 0xaaaa;
            bg_color = alpha_blend_16(bg_color, bg_add);
        }
        if (w->bar->urg & (1 << t)) {
            bg_add = color_8bit_to_16bit(molokai_red);
            bg_add.alpha = 0xaaaa;
            bg_color = alpha_blend_16(bg_color, bg_add);
        }
        if (w->bar->sel & (1 << t)) {
            bg_add = color_8bit_to_16bit(molokai_purple);
            bg_add.alpha = 0xaaaa;
            bg_color = alpha_blend_16(bg_color, bg_add);
        }
        TEXT( ww, num, P->awl_colors.fg_lay, bg_color );
        x += ww;
        width += ww;
    }
    return width;
}

static uint32_t taskbarwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix ) {
    uint32_t space = w->bar->center_widget_space;
    if (space == 0) return 0;

    awl_plugin_data_t* P = awl_plugin_get();
    if (!P) return 0;

    int n_windows = w->bar->n_tagwindows;
    if (n_windows <= 0) {
        TEXT( space, "", P->awl_colors.fg_win, P->awl_colors.bg_win_min );
        return 0;
    }

    // calculate space per window
    uint32_t spaces[n_windows];
    for (int wi=0; wi<n_windows; ++wi)
        spaces[wi] = 0;
    for (uint32_t s=0; s<space; ++s)
        spaces[s%n_windows]++;
    int nospace = 0;
    for (int wi=0; wi<n_windows; ++wi)
        if (spaces[wi] <= 20)
            nospace = 1;

    if (nospace)
        return TEXT( space, "+++", P->awl_colors.fg_win, P->awl_colors.bg_win_urg );

    drwl_window_t* windows = w->bar->tagwindows;

    // max, float, top
    for (int wi=0; wi<n_windows; ++wi) {
        char txt[256] = {0};
        if (windows[wi].floating || windows[wi].maximized || windows[wi].ontop) {
            strcat( txt, "[" );
            if (windows[wi].floating) strcat( txt, "F" );
            if (windows[wi].maximized) strcat( txt, "M" );
            if (windows[wi].ontop) strcat( txt, "T" );
            strcat( txt, "] " );
        }
        strcat( txt, windows[wi].name );
        TEXT( spaces[wi], txt, P->awl_colors.fg_win,
                windows[wi].focused ? P->awl_colors.bg_win_act :
                windows[wi].urgent  ? P->awl_colors.bg_win_urg :
                windows[wi].visible ? P->awl_colors.bg_win     :
                                      P->awl_colors.bg_win_min );
        x += spaces[wi];
    }
    return 0;
}

static uint32_t layoutwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix ) {
    awl_plugin_data_t* P = awl_plugin_get();
    if (!P) return 0;
    int ww = TEXTW(w->bar->m, "XXX");
    TEXT( ww, w->bar->m->ltsymbol, P->awl_colors.fg_lay, P->awl_colors.bg_lay );
    return ww;
}

static uint32_t clockwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix ) {
    int ww = TEXTW( w->bar->m, "--:--" );
    awl_plugin_data_t* P = awl_plugin_get();
    if (!P) return ww;

    char timestr[16] = "--:--";
    if (!sem_timedwait_nano(&P->date->sem, 1e3)) {
        strncpy(timestr, P->date->s, 15);
        w->age = 0;
        sem_post(&P->date->sem);
    } else {
        w->age++;
    }

    TEXT( ww, timestr, P->awl_colors.fg_lay, P->awl_colors.bg_lay );
    return ww;
}

static uint32_t systray_draw( widget_t* w, uint32_t x, pixman_image_t* pix ) {
    drwl_rect_color2( w->bar, x, 0, w->width, w->bar->m->b.height, 1, awl_plugin_get()->awl_colors.bg_lay );
    return w->width;
}

static uint32_t pulsewidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix ) {
    awl_plugin_data_t* P = awl_plugin_get();
    if (!P || !P->pulse) return 0;
    char string[32] = {0};
    pixman_color_t _molokai_red = color_8bit_to_16bit(molokai_red),
                   _molokai_orange = color_8bit_to_16bit(molokai_orange);
    float val = atomic_load( &P->pulse->value );
    int muted = atomic_load( &P->pulse->muted );
    int headphones = atomic_load( &P->pulse->headphones ) > 0;
    /*const char headphones_str[] = "🎧";*/
    /*const char speakers_str[] = "🔈";*/
    sprintf( string, "%s%3.0f%%", headphones ? "H" : "S", val * 100.0f );
    int ww = TEXTW( w->bar->m, "V___%" );
    pixman_color_t fg = (muted?_molokai_orange : lround(val*100.0)>100?_molokai_red : P->awl_colors.fg_lay);
    TEXT( ww, string, fg, P->awl_colors.bg_lay );
    return ww;
}

typedef struct {
    awl_stats_t stats;
    pixman_box32_t boxes[128*3*2];
} statuswidget_userdata_t;

static uint32_t statuswidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix ) {
    awl_plugin_data_t* P = awl_plugin_get();
    if (!P || !P->stats) return 16*3;

    if (!w->userdata)
        w->userdata = calloc(1, sizeof(statuswidget_userdata_t));
    statuswidget_userdata_t* u = w->userdata;
    pixman_box32_t* widget_boxes = u->boxes;
    awl_stats_t* st = &u->stats;

    if (!sem_timedwait_nano( &P->stats->sem, 1e3 )) {
        w->age = 0;
        memcpy( st, P->stats, sizeof(awl_stats_t) );
        sem_post( &P->stats->sem );
    } else {
        w->age++;
    }

    uint32_t widget_width = st->ncpu + st->nmem + st->nswp;
    const int ncpu = st->ncpu,
              nmem = st->nmem,
              nswp = st->nswp;
    const float *icpu = st->cpu,
                *imem = st->mem,
                *iswp = st->swp;
    pixman_box32_t *b_cpu = widget_boxes;
    pixman_box32_t *b_mem = b_cpu + ncpu;
    pixman_box32_t *b_swp = b_mem + nmem;
    pixman_box32_t *b_bg = b_swp + nswp;
    pixman_box32_t *b_bg_run = b_bg;

    int bar_height = w->bar->m->b.height;
    int xx=x;
    if (icpu) {
        for (int i=0; i<ncpu; ++i) {
            int ydiv = bar_height - icpu[st->dir ? ncpu-i : i] * bar_height;
            *b_bg_run++ = (pixman_box32_t){.x1=xx,.x2=xx+1,.y1=0, .y2=ydiv};
            b_cpu[i] = (pixman_box32_t){.x1=xx,.x2=xx+1,.y1=ydiv,.y2=bar_height};
            xx++;
        }
    }
    if (imem) {
        for (int i=0; i<nmem; ++i) {
            int ydiv = bar_height - imem[st->dir ? ncpu-i : i] * bar_height;
            *b_bg_run++ = (pixman_box32_t){.x1=xx,.x2=xx+1,.y1=0, .y2=ydiv};
            b_mem[i] = (pixman_box32_t){.x1=xx,.x2=xx+1,.y1=ydiv,.y2=bar_height};
            xx++;
        }
    }
    if (iswp) {
        for (int i=0; i<nswp; ++i) {
            int ydiv = bar_height - iswp[st->dir ? ncpu-i : i] * bar_height;
            *b_bg_run++ = (pixman_box32_t){.x1=xx,.x2=xx+1,.y1=0, .y2=ydiv};
            b_swp[i] = (pixman_box32_t){.x1=xx,.x2=xx+1,.y1=ydiv,.y2=bar_height};
            xx++;
        }
    }

    pixman_color_t bgcolor = P->awl_colors.bg_stats;
    for (int a=0; a<w->age; ++a) bgcolor = mean_color_16( bgcolor, P->awl_colors.bg_status, 0.9 );

    pixman_image_fill_boxes(PIXMAN_OP_SRC, pix, &bgcolor, b_bg_run-b_bg, b_bg);
    pixman_image_fill_boxes(PIXMAN_OP_SRC, pix, &P->awl_colors.fg_stats_cpu, ncpu, b_cpu);
    pixman_image_fill_boxes(PIXMAN_OP_SRC, pix, &P->awl_colors.fg_stats_mem, nmem, b_mem);
    pixman_image_fill_boxes(PIXMAN_OP_SRC, pix, &P->awl_colors.fg_stats_swp, nswp, b_swp);

    return widget_width;
}

static uint32_t tempwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix ) {
    Drwl* bar = w->bar;
    awl_plugin_data_t* P = awl_plugin_get();
    if (!P) return 0;
    if (!P->temp) return 0;

    if (!w->userdata) w->userdata = calloc(1,sizeof(awl_temperature_t));
    awl_temperature_t* T = w->userdata;

    if (!sem_timedwait_nano( &P->temp->sem, 10e6 )) {
        w->age = 0;
        memcpy( T, P->temp, sizeof(awl_temperature_t) );
        sem_post( &P->temp->sem );
    } else {
        w->age++;
    }

    if (T->ntemps == 0)
        return TEXTW(bar->m, "37°C");

    pixman_color_t bgcolor = P->awl_colors.bg_status;
    for (int a=0; a<w->age; ++a) bgcolor = mean_color_16( bgcolor, white, 0.9 );

    uint32_t width = 0;
    for (int i=0; i<T->ntemps; ++i) {
        char text[128] = {0};
        // only put the label if the string is set
        if (*T->f_labels[T->idx[i]])
            snprintf( text, 127, "%s:%.0f°C", T->f_labels[T->idx[i]], T->temps[i] );
        else
            snprintf( text, 127, "%.0f°C", T->temps[i] );
        pixman_color_t fgcolor = color_8bit_to_16bit(
                P->temp_color( T->temps[i], T->f_t_min[T->idx[i]], T->f_t_max[T->idx[i]] ) );
        uint32_t ww = TEXTW(bar->m, text);
        TEXT( ww, text, fgcolor, bgcolor );
        width += ww;
        x += ww;
    }

    return width;
}

static uint32_t batwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix ) {
    Drwl* bar = w->bar;
    awl_plugin_data_t* P = awl_plugin_get();
    if (!P) return 0;
    if (!P->bat) return 0;

    int charging = atomic_load( &P->bat->charging );
    float charge = atomic_load( &P->bat->charge );
    if (P->bat->charging < 0) return 0;

    char text[16] = {0};
    snprintf( text, 15, "%3.0f%%", charge * 100.0 );

    pixman_color_t fgcolor = P->awl_colors.fg_status;
    if (charge < 0.3)   fgcolor = color_8bit_to_16bit( molokai_orange );
    if (charge < 0.15)  fgcolor = color_8bit_to_16bit( molokai_red );
    if (charging)       fgcolor = color_8bit_to_16bit( molokai_green );
    uint32_t ww = TEXTW( bar->m, text );
    TEXT( ww, text, fgcolor, P->awl_colors.bg_status );
    return ww;
}

static uint32_t ipwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix ) {
    Drwl* bar = w->bar;
    awl_plugin_data_t* P = awl_plugin_get();
    if (!P) return 0;
    if (!P->ip) return 0;

    char placeholder[] = "    invalid   ";
    char address_[128] = {0};

    char* address = address_;
    if (!sem_timedwait_nano( &P->ip->sem, 10e6 )) {
        w->age = 0;
        strncpy( address, P->ip->address, 128 );
        sem_post( &P->ip->sem );
    } else {
        address = placeholder;
        w->age++;
    }
    int is_online = atomic_load( &P->ip->is_online );
    if (!*address) address = placeholder;

    pixman_color_t fgcolor = is_online ? color_8bit_to_16bit(molokai_green) :
                                         color_8bit_to_16bit(molokai_red);
    pixman_color_t bgcolor = P->awl_colors.bg_status;
    for (int a=0; a<w->age; ++a) bgcolor = mean_color_16( bgcolor, white, 0.9 );
    uint32_t ww = TEXTW( bar->m, address );
    TEXT( ww, address, fgcolor, bgcolor );
    return ww;
}
