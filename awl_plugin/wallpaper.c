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
#include "../awl_pthread.h"

#include <glob.h>
#include <pthread.h>
#include <semaphore.h>
#include <uthash.h>
#include <time.h>

#define MIN(A,B) ((A)<(B)?(A):(B))
#define MAX(A,B) ((A)>(B)?(A):(B))

typedef struct {
    pixman_image_t* img;
    time_t time;
    uint64_t memsz;

    UT_hash_handle hh;
    u128t id;
} wp_cache_t;

typedef struct awl_wallpaper_data_t {
    glob_t glob;
    int number,
        index;
    bool random,
         show_dirent,
         show_dirent_hidden;

    awl_dirent_t* dirent;
    sem_t dirent_sem;

    int idx_thread_update_sec;
    pthread_t idx_thread;
    sem_t idx_sem;

    AWL_Window* w;

    wp_cache_t* cache;
    sem_t cache_sem;
    int64_t cache_count_max,
            cache_bytes_max;
    int cache_thread_update_sec;
    pthread_t cache_thread;

    pthread_t update_display_thread;
} awl_wallpaper_data_t;

static int fast_random( int max );
static void* wp_update_display( void* );

static void wallpaper_draw( AWL_SingleWindow* win, pixman_image_t* final );
static void wallpaper_click( AWL_SingleWindow* win, int button );
static void wallpaper_dbus_hook( const char* signal, void* userdata );

static int next( const awl_wallpaper_data_t* d );
static void wallpaper_action( awl_wallpaper_data_t* d, int button );
static void* wp_idx_thread_fun( void* arg );

static int wp_cache_cleaner_cmp( const wp_cache_t* A, const wp_cache_t* B );
static void wp_cache_cleaner_once(awl_wallpaper_data_t* d);
void* wp_cache_cleaner( void* arg );

awl_wallpaper_data_t* wallpaper_init( const char* fname, int update_seconds ) {
    awl_wallpaper_data_t* d = calloc(1, sizeof(awl_wallpaper_data_t));
    d->show_dirent = 1;
    d->show_dirent_hidden = 1;
    d->random = 1;
    d->cache_count_max = 120;
    d->cache_bytes_max = 1024ul * 1024ul * 512ul;

    d->dirent = calloc(1, sizeof(awl_dirent_t));

    sem_init( &d->dirent_sem, 0, 1 );
    sem_init( &d->idx_sem, 0, 1 );
    sem_init( &d->cache_sem, 0, 1 );

    if (glob( fname, 0, NULL, &d->glob )) d->number = 0;
    else                                  d->number = d->glob.gl_pathc;

    char path[1024] = {0}; strcat(path, getenv("HOME")); strcat(path, "/Desktop/");
    *d->dirent = awl_dirent_create(path);

    awl_minimal_window_props_t p = awl_minimal_window_props_defaults;
    p.hidden = 0;
    p.anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
    p.layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
    p.name = fname;
    p.only_current_output = 0;
    p.draw = wallpaper_draw;
    p.click = wallpaper_click;

    // start with sth random :)
    if (d->random) d->index = next( d );
    d->idx_thread_update_sec = update_seconds;
    AWL_PTHREAD_CREATE( &d->idx_thread, NULL, &wp_idx_thread_fun, d );

    d->cache_thread_update_sec = update_seconds*(3./0.987);
    AWL_PTHREAD_CREATE( &d->cache_thread, NULL, &wp_cache_cleaner, d );

    d->w = awl_minimal_window_setup( &p );
    awl_minimal_window_set_userdata( d->w, d );
    AWL_PTHREAD_CREATE( &d->update_display_thread, NULL, &wp_update_display, d );

    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (B && B->dbus) B->dbus_add_callback( B->dbus, "wallpaper", &wallpaper_dbus_hook, d );

    return d;
}

void wallpaper_destroy( awl_wallpaper_data_t* d ) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (B && B->dbus) B->dbus_remove_callback( B->dbus, "wallpaper" );

    awl_minimal_window_destroy( d->w );
    if (d->number) globfree( &d->glob );
    awl_dirent_destroy( *d->dirent );

    // gather the threads
    sem_wait( &d->cache_sem );
    if (!pthread_cancel( d->cache_thread )) pthread_join( d->cache_thread, NULL );
    struct wl_display *display;
    pthread_join( d->update_display_thread, (void**)&display );
    if (display) wl_display_disconnect(display);
    if (!pthread_cancel( d->idx_thread )) pthread_join( d->idx_thread, NULL );

    // clear the cache
    wp_cache_t *wp, *tmp;
    int num_cleared = 0;
    HASH_ITER(hh, d->cache, wp, tmp) {
        HASH_DEL(d->cache, wp);
        pixman_image_unref(wp->img);
        free(wp);
        num_cleared++;
    }
    P_awl_log_printf( "wallpaper_destroy(): cleared %i cache entries", num_cleared );

    sem_destroy( &d->cache_sem );
    sem_destroy( &d->idx_sem );
    sem_destroy( &d->dirent_sem );

    free( d->dirent );
    free( d );
}

static void handle_global(void *data, struct wl_registry *registry,
          uint32_t name, const char *interface, uint32_t version) {
    (void)registry; (void)name; (void)version;
    awl_wallpaper_data_t* d = (awl_wallpaper_data_t*)data;
    if (!d->w) return;
    if (!strcmp(interface, wl_output_interface.name)) awl_minimal_window_refresh( d->w );
}
static void handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}
static void* wp_update_display( void* arg ) {
    awl_wallpaper_data_t* d = (awl_wallpaper_data_t*)arg;
    if (!d) return NULL;
    awl_minimal_window_wait_ready( d->w );
    // add the wallpaper listener for new outputs
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) P_awl_err_printf( "Failed to create display" );
    struct wl_registry *registry = wl_display_get_registry(display);
    const struct wl_registry_listener registry_listener = {
        .global = handle_global,
        .global_remove = handle_global_remove
    };
    wl_registry_add_listener(registry, &registry_listener, d);
    wl_display_roundtrip(display);
    return display;
}

static pixman_transform_t transform_wp_to_screen( pixman_image_t* img, int w, int h ) {
    float wp = pixman_image_get_width(img),
          hp = pixman_image_get_height(img),
          ww = w,
          hw = h;
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
    return t2;
}

static void wallpaper_draw( AWL_SingleWindow* win, pixman_image_t* final ) {
    awl_wallpaper_data_t* d = awl_minimal_window_get_userdata( win->parent );
    if (!d) return;

    P_awl_log_printf("in wallpaper draw function");

    if (!d->number) return;
    char* wpfname = d->glob.gl_pathv[d->index];

    wp_cache_t *found = NULL;
    u128t md5 = awl_md5sum_int( wpfname, strlen(wpfname) );

    wp_cache_cleaner_once(d);

    sem_wait( &d->cache_sem );
    HASH_FIND(hh, d->cache, &md5, sizeof(u128t), found);
    if (!found) {
        found = calloc(1,sizeof(wp_cache_t));
        found->id = md5;
        found->time = time(NULL);

        FILE* f = fopen(wpfname, "r");
        if (!f) {
            P_awl_err_printf("could not open file %s", wpfname);
            return;
        }
        found->img = awl_png_load(f, wpfname);
        /* pixman_image_set_filter(found->img, PIXMAN_FILTER_BEST, NULL, 0); */
        fclose(f);
        found->memsz = ((uint64_t)PIXMAN_FORMAT_BPP(pixman_image_get_format(found->img)) *
                            pixman_image_get_width(found->img) * pixman_image_get_height(found->img))/8 +
                       sizeof(wp_cache_t);
        HASH_ADD(hh, d->cache, id, sizeof(u128t), found);
    }

    pixman_transform_t t = transform_wp_to_screen( found->img, win->width, win->height );
    pixman_image_set_transform( found->img, &t );
    pixman_image_composite32(PIXMAN_OP_OVER, found->img, NULL, final, 0, 0, 0, 0, 0, 0, win->width, win->height);
    sem_post( &d->cache_sem );

    if (d->show_dirent) {
        pixman_image_t *fg = pixman_image_create_bits(PIXMAN_a8r8g8b8, win->width, MIN(win->height, 140),
                NULL, win->width*4);
        sem_wait( &d->dirent_sem );
        awl_dirent_update( d->dirent );
        sem_post( &d->dirent_sem );
        uint32_t x = 0, dx = 20, y = 50, dy = 70;
        char str[512];
        pixman_color_t fg_color = black,
                       bg_color = color_8bit_to_16bit( 0x88888888 );

        for (char** F = d->dirent->v[awl_dirent_type_f]; *F; F++) {
            strcpy( str, " " ); strcat( str, *F ); strcat( str, " " ); x += dx;
            x = draw_text_at( str, x, y, fg, final, &fg_color, &bg_color, win->width, 10, 15 );
        }
        if (d->show_dirent_hidden) {
            for (char** F = d->dirent->v[awl_dirent_type_f_h]; *F; F++) {
                strcpy( str, " " ); strcat( str, *F ); strcat( str, " " ); x += dx;
                x = draw_text_at( str, x, y, fg, final, &fg_color, &bg_color, win->width, 10, 15 );
            }
        }

        y += dy;
        x = 0;
        for (char** F = d->dirent->v[awl_dirent_type_d]; *F; F++) {
            strcpy( str, " " ); strcat( str, *F ); strcat( str, "/ " );
            x += dx;
            x = draw_text_at( str, x, y, fg, final, &fg_color, &bg_color, win->width, 10, 15 );
        }
        if (d->show_dirent_hidden) {
            for (char** F = d->dirent->v[awl_dirent_type_d_h]; *F; F++) {
                strcpy( str, " " ); strcat( str, *F ); strcat( str, "/ " );
                x += dx;
                x = draw_text_at( str, x, y, fg, final, &fg_color, &bg_color, win->width, 10, 15 );
            }
        }

        y += dy;
        x = 0;
        for (char** F = d->dirent->v[awl_dirent_type_b]; *F; F++) {
            strcpy( str, " 🗲" ); strcat( str, *F ); strcat( str, "🗲 " );
            x += dx;
            x = draw_text_at( str, x, y, fg, final, &fg_color, &bg_color, win->width, 10, 15 );
        }
        if (d->show_dirent_hidden) {
            for (char** F = d->dirent->v[awl_dirent_type_b_h]; *F; F++) {
                strcpy( str, " 🗲" ); strcat( str, *F ); strcat( str, "🗲 " );
                x += dx;
                x = draw_text_at( str, x, y, fg, final, &fg_color, &bg_color, win->width, 10, 15 );
            }
        }

        pixman_image_composite32(PIXMAN_OP_OVER, fg, NULL, final,  0, 0, 0, 0, 0, 0, win->width, win->height);
        pixman_image_unref(fg);
    }
}

static int fast_random( int max ) {
    unsigned result = 0;
    FILE* f = fopen("/dev/urandom","r");
    fread( &result, sizeof(unsigned), 1, f );
    fclose( f );
    return result % max;
}

static int next( const awl_wallpaper_data_t* d ) {
    if (d->random) return fast_random(d->number);
    else return (d->index+1)%d->number;
}

static void wallpaper_action( awl_wallpaper_data_t* d, int button ) {
    switch (button) {
        case BTN_MIDDLE:
            d->show_dirent = !d->show_dirent;
            break;
        case BTN_LEFT:
            d->random = 0;
            d->index = next( d );
            break;
        case BTN_RIGHT:
            d->random = 1;
            d->index = next( d );
            break;
        default:
            d->index = next( d );
            break;
    }
}

static void* wp_idx_thread_fun( void* arg ) {
    awl_wallpaper_data_t* d = (awl_wallpaper_data_t*)arg;
    while (1) {
        sleep(MAX(d->idx_thread_update_sec,1));
        if (sem_trywait( &d->idx_sem ) == -1) continue;
        wallpaper_action( d, BTN_LEFT | BTN_MIDDLE | BTN_RIGHT );
        if (d->w) awl_minimal_window_refresh(d->w);
        sem_post( &d->idx_sem );
    }
    return NULL;
}

static void wallpaper_click( AWL_SingleWindow* win, int button ) {
    awl_wallpaper_data_t* d = awl_minimal_window_get_userdata( win->parent );
    if (!d) return;
    wallpaper_action( d, button );
    awl_minimal_window_refresh(d->w);
}

static int wp_cache_cleaner_cmp( const wp_cache_t* A, const wp_cache_t* B ) {
    if (A->time == B->time) {
        int64_t *Aid = (int64_t*)&A->id,
                *Bid = (int64_t*)&B->id;
        return Bid - Aid;
    } else {
        return B->time - A->time;
    }
}

static void wp_cache_cleaner_once(awl_wallpaper_data_t* d) {
    int count = HASH_COUNT(d->cache);
    if (count == 0) return;

    if (sem_trywait( &d->cache_sem ) == -1) return;

    int nwp_cached = 0;
    int64_t bytes = 0;
    wp_cache_t *wp, *tmp;
    HASH_ITER(hh, d->cache, wp, tmp) {
        nwp_cached++;
        bytes += wp->memsz;
    }

    if ((d->cache_count_max < 0 || d->cache_count_max >= nwp_cached) &&
        (d->cache_bytes_max < 0 || d->cache_bytes_max >= bytes))
        goto wp_cache_continue;

    P_awl_log_printf( "cleaning wallpaper cache" );
    int ncleaned_mem = 0, ncleaned_max = 0, ncleaned = 0, ntot = 0;
    int64_t nbytes = 0;
    HASH_SORT( d->cache, wp_cache_cleaner_cmp );
    HASH_ITER(hh, d->cache, wp, tmp) {
        nbytes += wp->memsz;
        ntot++;
        int condition_max = ntot > d->cache_count_max;
        int condition_mem = nbytes > d->cache_bytes_max;
        ncleaned_max += condition_max;
        ncleaned_mem += condition_mem;
        if (condition_max || condition_mem) {
            HASH_DEL(d->cache, wp);
            pixman_image_unref(wp->img);
            free(wp);
            ncleaned++;
        }
    }
    P_awl_log_printf( "cleaned %i/%i images from cache (%i:mem, %i:num)", ncleaned, ntot,
            ncleaned_mem, ncleaned_max );

wp_cache_continue:
    sem_post( &d->cache_sem );
}

void* wp_cache_cleaner( void* arg ) {
    awl_wallpaper_data_t* d = (awl_wallpaper_data_t*)arg;
    if (!d) return NULL;
    while (1) {
        P_awl_log_printf( "wallpaper cache cleaner…" );
        wp_cache_cleaner_once(d);
        sleep(MAX(d->cache_thread_update_sec,1));
    }
    return NULL;
}

static void wallpaper_dbus_hook( const char* signal, void* userdata ) {
    awl_wallpaper_data_t* d = (awl_wallpaper_data_t*)userdata;
    if (!strcmp( signal, "left" )) wallpaper_action( d, BTN_LEFT );
    else if (!strcmp( signal, "right" )) wallpaper_action( d, BTN_RIGHT );
    else if (!strcmp( signal, "middle" )) wallpaper_action( d, BTN_MIDDLE );
    else wallpaper_action( d, BTN_LEFT|BTN_RIGHT|BTN_MIDDLE );
    awl_minimal_window_refresh(d->w);
}

