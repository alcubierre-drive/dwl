#include "wallpaper.h"

#include "minimal_window.h"

#include "wbg_png.h"
#include "readdir.h"
#include "md5.h"

#include "init.h"
#include "bar.h"

#include "../awl.h"
#include "../awl_log.h"
#include "../awl_dbus.h"

#include <glob.h>
#include <pthread.h>
#include <uthash.h>
#include <time.h>

#define MIN(A,B) ((A)<(B)?(A):(B))
#define MAX(A,B) ((A)>(B)?(A):(B))

static int fast_random( int max ) {
    unsigned result = 0;
    FILE* f = fopen("/dev/urandom","r");
    fread( &result, sizeof(unsigned), 1, f );
    fclose( f );
    return result % max;
}

static glob_t wallpaper_glob = {0};
static int wallpaper_number = 0;
static int wallpaper_index = 0;

static int wallpaper_random = 1;

static int wallpaper_show_dirent = 1,
           wallpaper_show_dirent_hidden = 1;
static awl_dirent_t wallpaper_dirent = {0};

static int wp_idx_thread_update = 0;
static pthread_t wp_idx_thread = {0};

static AWL_Window* w = NULL;

struct wp_cache {
    pixman_image_t* img;
    time_t time;
    uint64_t memsz;

    UT_hash_handle hh;
    u128t id;
};
static struct wp_cache* wp_cache = NULL;
static pthread_mutex_t wp_cache_mtx = PTHREAD_MUTEX_INITIALIZER;
static int wp_cache_count_max = 120; // disable both count and bytes with <0
static int64_t wp_cache_bytes_max = 1024ul * 1024ul * 1024ul; // huge cache (512MiB)
static int wp_cache_thread_update = 0;
static pthread_t wp_cache_thread = {0};
static void wp_cache_cleaner_once(void);

static pthread_t wp_update_display_thread = {0};
static void* wp_update_display( void* );

static void wallpaper_draw( AWL_SingleWindow* win, pixman_image_t* final ) {
    P_awl_log_printf("in wallpaper draw function");

    if (!wallpaper_number) return;
    char* wpfname = wallpaper_glob.gl_pathv[wallpaper_index];

    struct wp_cache item = {0},
                    *found = NULL;

    char* wpfname_res = calloc(1, strlen(wpfname) + 64);
    sprintf( wpfname_res, "%s@%ix%i", wpfname, win->width, win->height );
    u128t md5 = awl_md5sum_int( wpfname_res, strlen(wpfname) );
    free( wpfname_res );
    item.id = md5;

    wp_cache_cleaner_once();

    pthread_mutex_lock( &wp_cache_mtx );

    HASH_FIND(hh, wp_cache, &item.id, sizeof(u128t), found);
    if (!found) {
        FILE* f = fopen(wpfname, "r");
        if (!f) {
            P_awl_err_printf("could not open file %s", wpfname);
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
        pixman_image_t* bg = pixman_image_create_bits(PIXMAN_a8r8g8b8, win->width, win->height, NULL, win->width*4);
        pixman_image_composite32(PIXMAN_OP_OVER, png_image, NULL, bg, 0, 0, 0, 0, 0, 0, win->width, win->height);
        pixman_image_unref(png_image);

        found = calloc(1,sizeof(struct wp_cache));
        found->id = md5;
        found->img = bg;
        found->time = time(NULL);
        found->memsz = ((uint64_t)PIXMAN_FORMAT_BPP(pixman_image_get_format(bg)) *
                                  pixman_image_get_width(bg) * pixman_image_get_height(bg))/8 +
                       sizeof(struct wp_cache);

        HASH_ADD(hh, wp_cache, id, sizeof(u128t), found);
    }

    pthread_mutex_unlock( &wp_cache_mtx );

    // set wallpaper from cache
    pixman_image_composite32(PIXMAN_OP_OVER, found->img, NULL, final, 0, 0, 0, 0, 0, 0, win->width, win->height);

    if (wallpaper_show_dirent) {
        pixman_image_t *fg = pixman_image_create_bits(PIXMAN_a8r8g8b8, win->width, MIN(win->height, 140), NULL, win->width*4);
        awl_dirent_update( &wallpaper_dirent );
        uint32_t x = 0, dx = 20, y = 50, dy = 70;
        char str[512];
        pixman_color_t fg_color = black, bg_color = color_8bit_to_16bit( 0x88888888 );

        for (char** F = wallpaper_dirent.v[awl_dirent_type_f]; *F; F++) {
            strcpy( str, " " ); strcat( str, *F ); strcat( str, " " ); x += dx;
            x = draw_text_at( str, x, y, fg, final, &fg_color, &bg_color, win->width, 10, 15 );
        }
        if (wallpaper_show_dirent_hidden) {
            for (char** F = wallpaper_dirent.v[awl_dirent_type_f_h]; *F; F++) {
                strcpy( str, " " ); strcat( str, *F ); strcat( str, " " ); x += dx;
                x = draw_text_at( str, x, y, fg, final, &fg_color, &bg_color, win->width, 10, 15 );
            }
        }

        y += dy;
        x = 0;
        for (char** F = wallpaper_dirent.v[awl_dirent_type_d]; *F; F++) {
            strcpy( str, " " ); strcat( str, *F ); strcat( str, "/ " );
            x += dx;
            x = draw_text_at( str, x, y, fg, final, &fg_color, &bg_color, win->width, 10, 15 );
        }
        if (wallpaper_show_dirent_hidden) {
            for (char** F = wallpaper_dirent.v[awl_dirent_type_d_h]; *F; F++) {
                strcpy( str, " " ); strcat( str, *F ); strcat( str, "/ " );
                x += dx;
                x = draw_text_at( str, x, y, fg, final, &fg_color, &bg_color, win->width, 10, 15 );
            }
        }

        y += dy;
        x = 0;
        for (char** F = wallpaper_dirent.v[awl_dirent_type_b]; *F; F++) {
            strcpy( str, " 🗲" ); strcat( str, *F ); strcat( str, "🗲 " );
            x += dx;
            x = draw_text_at( str, x, y, fg, final, &fg_color, &bg_color, win->width, 10, 15 );
        }
        if (wallpaper_show_dirent_hidden) {
            for (char** F = wallpaper_dirent.v[awl_dirent_type_b_h]; *F; F++) {
                strcpy( str, " 🗲" ); strcat( str, *F ); strcat( str, "🗲 " );
                x += dx;
                x = draw_text_at( str, x, y, fg, final, &fg_color, &bg_color, win->width, 10, 15 );
            }
        }

        pixman_image_composite32(PIXMAN_OP_OVER, fg, NULL, final,  0, 0, 0, 0, 0, 0, win->width, win->height);
        pixman_image_unref(fg);
    }
}


static int next( int num ) {
    if (wallpaper_random) return fast_random(num);
    else return (wallpaper_index+1)%wallpaper_number;
}

static void wallpaper_action( int button ) {
    switch (button) {
        case BTN_MIDDLE:
            wallpaper_show_dirent = !wallpaper_show_dirent;
            break;
        case BTN_LEFT:
            wallpaper_random = 0;
            wallpaper_index = next( wallpaper_number );
            break;
        case BTN_RIGHT:
            wallpaper_random = 1;
            wallpaper_index = next( wallpaper_number );
            break;
        default:
            wallpaper_index = next( wallpaper_number );
            break;
    }
}

static void* wp_idx_thread_fun( void* arg ) {
    (void)arg;
    while (1) {
        sleep(MAX(wp_idx_thread_update,1));
        wallpaper_action( BTN_LEFT | BTN_MIDDLE | BTN_RIGHT );
        awl_minimal_window_refresh(w);
        /* char cmd[128]; */
        /* sprintf( cmd, "notify-send 'wallpaper %i/%i'", idx, wallpaper_number ); */
        /* system(cmd); */
    }
    return NULL;
}

static void wallpaper_click( AWL_SingleWindow* win, int button ) {
    (void)win;
    wallpaper_action( button );
    awl_minimal_window_refresh(w);
    /* char cmd[128]; */
    /* sprintf( cmd, "notify-send 'wallpaper %i/%i'", idx, wallpaper_number ); */
    /* system(cmd); */
}

int wp_cache_cleaner_cmp( const struct wp_cache* A, const struct wp_cache* B ) {
    if (A->time == B->time) {
        int64_t *Aid = (int64_t*)&A->id,
                *Bid = (int64_t*)&B->id;
        return Bid - Aid;
    } else {
        return B->time - A->time;
    }
}

static void wp_cache_cleaner_once(void) {
    pthread_mutex_lock( &wp_cache_mtx );

    struct wp_cache *wp, *tmp;

    int nwp_cached = 0;
    int64_t bytes = 0;
    HASH_ITER(hh, wp_cache, wp, tmp) {
        nwp_cached++;
        bytes += wp->memsz;
    }

    if ((wp_cache_count_max < 0 || wp_cache_count_max >= nwp_cached) &&
        (wp_cache_bytes_max < 0 || wp_cache_bytes_max >= bytes))
        goto wp_cache_continue;

    P_awl_log_printf( "cleaning wallpaper cache" );
    int ncleaned_mem = 0, ncleaned_max = 0, ncleaned = 0, ntot = 0;
    int64_t nbytes = 0;
    HASH_SORT( wp_cache, wp_cache_cleaner_cmp );
    HASH_ITER(hh, wp_cache, wp, tmp) {
        nbytes += wp->memsz;
        ntot++;
        int condition_max = ntot > wp_cache_count_max;
        int condition_mem = nbytes > wp_cache_bytes_max;
        ncleaned_max += condition_max;
        ncleaned_mem += condition_mem;
        if (condition_max || condition_mem) {
            HASH_DEL(wp_cache, wp);
            pixman_image_unref(wp->img);
            free(wp);
            ncleaned++;
        }
    }
    P_awl_log_printf( "cleaned %i/%i images from cache (%i:mem, %i:num)", ncleaned, ntot, ncleaned_mem, ncleaned_max );

wp_cache_continue:
    pthread_mutex_unlock( &wp_cache_mtx );
}

void* wp_cache_cleaner(void* arg) {
    (void)arg;
    while (1) {
        P_awl_log_printf( "wallpaper cache cleaner…" );
        wp_cache_cleaner_once();
        sleep(MAX(wp_cache_thread_update,1));
    }
    return NULL;
}

static void wallpaper_dbus_hook( const char* signal, void* userdata ) {
    (void)userdata;
    if (!strcmp( signal, "left" )) wallpaper_action( BTN_LEFT );
    else if (!strcmp( signal, "right" )) wallpaper_action( BTN_RIGHT );
    else if (!strcmp( signal, "middle" )) wallpaper_action( BTN_MIDDLE );
    else wallpaper_action( BTN_LEFT|BTN_RIGHT|BTN_MIDDLE );
    awl_minimal_window_refresh(w);
}
void wallpaper_init( const char* fname, int update_seconds ) {
    if (glob( fname, 0, NULL, &wallpaper_glob ))
        wallpaper_number = 0;
    else
        wallpaper_number = wallpaper_glob.gl_pathc;

    char path[1024] = {0};
    strcat(path, getenv("HOME"));
    strcat(path, "/Desktop/");
    wallpaper_dirent = awl_dirent_create(path);

    awl_minimal_window_props_t p = awl_minimal_window_props_defaults;
    p.hidden = 0;
    p.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    p.layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
    p.name = fname;
    p.only_current_output = 0;
    p.draw = wallpaper_draw;
    p.click = wallpaper_click;

    // start with sth random :)
    if (wallpaper_random)
        wallpaper_index = next( wallpaper_number );
    wp_idx_thread_update = update_seconds;
    pthread_create( &wp_idx_thread, NULL, &wp_idx_thread_fun, NULL );

    wp_cache_thread_update = update_seconds*(3./0.987);
    pthread_create( &wp_cache_thread, NULL, &wp_cache_cleaner, NULL );

    w = awl_minimal_window_setup( &p );
    pthread_create( &wp_update_display_thread, NULL, &wp_update_display, NULL );

    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (B && B->dbus) B->dbus_add_callback( B->dbus, "wallpaper", &wallpaper_dbus_hook, NULL );
}

void wallpaper_destroy( void ) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (B && B->dbus) B->dbus_remove_callback( B->dbus, "wallpaper" );

    awl_minimal_window_destroy( w );
    if (wallpaper_number)
        globfree( &wallpaper_glob );
    awl_dirent_destroy( wallpaper_dirent );

    pthread_mutex_lock( &wp_cache_mtx );
    pthread_cancel( wp_cache_thread );
    pthread_join( wp_cache_thread, NULL );
    pthread_mutex_unlock( &wp_cache_mtx );

    struct wl_display *display;
    pthread_join( wp_update_display_thread, (void**)&display );
    wl_display_disconnect(display);

    struct wp_cache *wp, *tmp;
    HASH_ITER(hh, wp_cache, wp, tmp) {
        HASH_DEL(wp_cache, wp);
        pixman_image_unref(wp->img);
        free(wp);
    }

    pthread_cancel( wp_idx_thread );
    pthread_join( wp_idx_thread, NULL );
}

static void handle_global(void *data, struct wl_registry *registry,
          uint32_t name, const char *interface, uint32_t version) {
    (void)data; (void)registry; (void)name; (void)version;
    if (!strcmp(interface, wl_output_interface.name)) awl_minimal_window_refresh( w );
}
static void handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}
static void* wp_update_display( void* arg ) {
    (void)arg;
    awl_minimal_window_wait_ready( w );
    // add the wallpaper listener for new outputs
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) P_awl_err_printf( "Failed to create display" );
    struct wl_registry *registry = wl_display_get_registry(display);
    const struct wl_registry_listener registry_listener = {
        .global = handle_global,
        .global_remove = handle_global_remove
    };
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);
    return display;
}

