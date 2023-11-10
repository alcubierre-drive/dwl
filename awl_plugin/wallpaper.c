#include "wallpaper.h"
#include "minimal_window.h"
#include "wbg_png.h"
#include "../awl.h"
#include "../awl_log.h"
#include <glob.h>
#include <pthread.h>

static glob_t wallpaper_glob = {0};
static int wallpaper_number = 0;
static int wallpaper_index = 0;

static int wp_idx_thread_update = 0;
static pthread_t wp_idx_thread = {0};

static AWL_Window* w = NULL;

static void wallpaper_draw( AWL_SingleWindow* win, pixman_image_t* fg, pixman_image_t* bg ) {
    (void)bg;

    awl_log_printf("in wallpaper draw function");

    if (!wallpaper_number) return;
    char* wpfname = wallpaper_glob.gl_pathv[wallpaper_index];

    FILE* f = fopen(wpfname, "r");
    if (!f) {
        awl_err_printf("could not open file %s", wpfname);
        return;
    }
    pixman_image_t* png_image = awl_png_load(f, wpfname);
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

static void* wp_idx_thread_fun( void* arg ) {
    (void)arg;
    while (1) {
        sleep(wp_idx_thread_update);
        int idx = (wallpaper_index+1)%wallpaper_number;
        wallpaper_index = idx;
        awl_minimal_window_refresh(w);
    }
    return NULL;
}

static void wallpaper_click( AWL_SingleWindow* win, int button ) {
    (void)win;
    (void)button;
    int idx = (wallpaper_index+1)%wallpaper_number;
    wallpaper_index = idx;
    awl_minimal_window_refresh(w);
}

void wallpaper_init( const char* fname, int update_seconds ) {
    if (glob( fname, 0, NULL, &wallpaper_glob ))
        wallpaper_number = 0;
    else
        wallpaper_number = wallpaper_glob.gl_pathc;

    awl_minimal_window_props_t p = awl_minimal_window_props_defaults;
    p.hidden = 0;
    p.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT|ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    p.layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
    p.name = fname;
    p.only_current_output = 0;
    p.draw = wallpaper_draw;
    p.click = wallpaper_click;

    wp_idx_thread_update = update_seconds;
    pthread_create( &wp_idx_thread, NULL, &wp_idx_thread_fun, NULL );
    w = awl_minimal_window_setup( &p );
}

void wallpaper_destroy( void ) {
    awl_minimal_window_destroy( w );
    if (wallpaper_number)
        globfree( &wallpaper_glob );
    pthread_cancel( wp_idx_thread );
    pthread_join( wp_idx_thread, NULL );
}
