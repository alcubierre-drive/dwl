#include "wallpaper.h"
#include "minimal_window.h"
#include "wbg_png.h"
#include "../awl.h"
#include "../awl_log.h"

static char wallpaper_fname[1024] = {0};
static AWL_Window* w = NULL;

static void wallpaper_draw( AWL_SingleWindow* win, pixman_image_t* fg, pixman_image_t* bg ) {
    awl_log_printf("in wallpaper draw function");
    (void)bg;
    FILE* f = fopen(wallpaper_fname, "r");
    if (!f) {
        awl_err_printf("could not open file %s", wallpaper_fname);
        return;
    }
    pixman_image_t* png_image = awl_png_load(f, wallpaper_fname);
    fclose(f);

    // best fit
    float wp = pixman_image_get_width(png_image),
          hp = pixman_image_get_height(png_image),
          ww = win->width,
          hw = win->height;
    float sx = wp / ww,
          sy = hp / hw;
    float s = sx > sy ? sy : sx;
    sx = s;
    sy = s;

    float tx = (wp / sx - ww) / 2 / sx,
          ty = (hp / sy - hw) / 2 / sy;
    pixman_f_transform_t t;
    pixman_transform_t t2;
    pixman_f_transform_init_translate(&t, tx, ty);
    pixman_f_transform_init_scale(&t, sx, sy);
    pixman_transform_from_pixman_f_transform(&t2, &t);
    pixman_image_set_transform(png_image, &t2);
    pixman_image_set_filter(png_image, PIXMAN_FILTER_BEST, NULL, 0);
    pixman_image_composite32(PIXMAN_OP_OVER, png_image, NULL, fg, 0, 0, 0, 0, 0, 0,
            win->width, win->height);
    pixman_image_unref(png_image);
}

void wallpaper_init( const char* fname ) {
    strcpy( wallpaper_fname, fname );
    awl_minimal_window_props_t p = awl_minimal_window_props_defaults;
    p.hidden = 0;
    p.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT|ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    p.layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
    p.name = wallpaper_fname;
    p.only_current_output = 0;
    p.draw = wallpaper_draw;
    w = awl_minimal_window_setup( &p );
}

void wallpaper_destroy( void ) {
    awl_minimal_window_destroy( w );
}
