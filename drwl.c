#include "drwl.h"
#include "plugins/colors.h"

int drwl_init(void) {
    return fcft_init(FCFT_LOG_COLORIZE_AUTO, 0, FCFT_LOG_CLASS_ERROR);
}

Drwl * drwl_create(Monitor* m) {
    Drwl *drwl;

    if (!(drwl = calloc(1, sizeof(Drwl))))
        return NULL;
    drwl->m = m;
    /* the widgets come from libawlplugins.so, see plugin_host.h */
    return drwl;
}

static void drwl_setfont(Drwl *drwl, struct fcft_font *font) {
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

static void drwl_rect_color(Drwl *drwl, int x, int y, unsigned int w, unsigned int h, uint32_t color) {
    pixman_color_t clr;
    if (!drwl || !drwl->pix)
        return;

    clr = convert_color(color);
    pixman_image_fill_rectangles(PIXMAN_OP_SRC, drwl->pix, &clr, 1,
        &(pixman_rectangle16_t){x, y, w, h});
}

/* width of `text` in pixels, kerning included */
static unsigned int text_width(Drwl *drwl, const char *text) {
    uint32_t cp = 0, last_cp = 0;
    long x = 0, x_kern;
    const struct fcft_glyph *glyph;

    while (*text) {
        text += utf8decode(text, &cp);
        if (!(glyph = fcft_rasterize_char_utf32(drwl->font, cp, FCFT_SUBPIXEL_DEFAULT)))
            continue;
        x_kern = 0;
        if (last_cp)
            fcft_kerning(drwl->font, last_cp, cp, &x_kern, NULL);
        last_cp = cp;
        x += x_kern + glyph->advance.x;
    }
    return x > 0 ? (unsigned int)x : 0;
}

static int drwl_text_color(Drwl *drwl, int x, int y, unsigned int w, unsigned int h,
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

    if (!drwl || (render && (!w || !drwl->pix)) || !text || !drwl->font)
        return 0;

    if (!render) {
        // w = ...;
    } else {
        clr = convert_color(fg);
        fg_pix = pixman_image_create_solid_fill(&clr);

        drwl_rect_color(drwl, x, y, w, y<0?h-y:h, bg);

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
        if (!glyph) {
            text += utf8charlen;
            continue;
        }

        x_kern = 0;
        if (last_cp)
            fcft_kerning(drwl->font, last_cp, cp, &x_kern, NULL);
        last_cp = cp;

        ty = y + (h - drwl->font->height) / 2 + drwl->font->ascent;

        /* draw ellipsis if remaining text doesn't fit */
        if (!noellipsis && x_kern + glyph->advance.x + eg->advance.x > w && *(text + 1) != '\0') {
            if ((int)text_width(drwl, text) - glyph->advance.x < eg->advance.x) {
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

unsigned int drwl_font_getwidth(Drwl *drwl, const char *text) {
    if (!drwl || !drwl->font || !text)
        return 0;
    return text_width(drwl, text);
}

void drwl_finish_drawing(Drwl *drwl) {
    if (drwl && drwl->pix) {
        pixman_image_unref(drwl->pix);
        /* drwl_destroy() also checks drwl->pix before unreffing it again on
         * monitor teardown; leaving this dangling causes a double-unref /
         * use-after-free on the pixman image (heap corruption that doesn't
         * crash until some later, unrelated allocation reuses the memory). */
        drwl->pix = NULL;
    }
}

void drwl_destroy(Drwl *drwl) {
    if (drwl->pix)
        pixman_image_unref(drwl->pix);
    if (drwl->font)
        drwl_destroy_font(drwl->font);

    drwl_widgets_clear(drwl);
    free(drwl);
}

void drwl_widgets_clear(Drwl *drwl) {
    for (int l=0; l<drwl->n_widgets_left; ++l)
        if (drwl->widgets_left[l].free)
            drwl->widgets_left[l].free( drwl->widgets_left[l].userdata );
    for (int r=0; r<drwl->n_widgets_right; ++r)
        if (drwl->widgets_right[r].free)
            drwl->widgets_right[r].free( drwl->widgets_right[r].userdata );

    if (drwl->has_center_widget)
        if (drwl->center_widget.free)
            drwl->center_widget.free( drwl->center_widget.userdata );

    drwl->n_widgets_left = drwl->n_widgets_right = 0;
    drwl->has_center_widget = 0;
    drwl->center_widget = (widget_t){0};
}

void drwl_fini(void) {
    fcft_fini();
}

