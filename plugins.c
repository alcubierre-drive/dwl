#include "dwl.h"
#include "plugins.h"
#include "plugins/ipaddr.h"
#include "plugins/stats.h"
#include "plugins/temp.h"
#include "plugins/bat.h"
#include "plugins/date.h"
#include "plugins/pulsetest.h"
#include "plugins/poller.h"
#include "plugins/wbg_png.h"
#include "plugins/redraw.h"

#include <stddef.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#ifdef USLEEP_NOT_DEFINED
int usleep(useconds_t usec);
#endif

#include "plugins/readdir.h"
#include <glob.h>

typedef struct {
    DesktopFiles df;

    char** files;
    int n_files;
    glob_t gl;

    sem_t sem;
    int index;
    int current_time;
    int expiry_time;

    struct { uint8_t
        random:1,
        show:1,
        hidden:1;
    };

    pixman_image_t* img;
} Wallpaper;

/*
static Wallpaper wp = {0};

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

static int fast_random( int max ) {
    unsigned result = 0;
    FILE* f = fopen("/dev/urandom","r");
    fread( &result, sizeof(unsigned), 1, f );
    fclose( f );
    return result % max;
}

static void wpfunc( pixman_image_t* pix, uint64_t op ) {
    return; // TODO
    sem_wait( &wp.sem );
    int advance = 0;
    switch (op) {
        case BTN_LEFT: advance = 1; break;
        case BTN_RIGHT: wp.show = !wp.show; break;
        case BTN_MIDDLE: wp.hidden = !wp.hidden; break;
        case BTN_LEFT+100: wp.random = !wp.random; break;
        default: break;
    }
    printf( "I'm in the wallpaper function with arg %lu\n", op );
    int update = findfiles( &wp.df, "/home/lennart/Desktop/" );
    int update_img = (wp.current_time >= wp.expiry_time) || (wp.img == NULL) || advance;
    if (update_img) {
        wp.current_time = 0;

        // find next image (currently no randomness!)
        if (wp.random) {
            wp.index = fast_random( wp.n_files );
        } else {
            wp.index++;
            if (wp.index >= wp.n_files) wp.index = 0;
        }

        // delete unused image
        if (wp.img) pixman_image_unref(wp.img);

        // load next image
        FILE* f = fopen( wp.files[wp.index], "r" );
        if (f) {
            wp.img = awl_png_load(f, wp.files[wp.index]);
            fclose(f);
        }
    }
    if (update || update_img) {
        int w = pixman_image_get_width(pix),
            h = pixman_image_get_height(pix);
        pixman_transform_t trafo = transform_wp_to_screen( wp.img, w, h );
        pixman_image_set_transform( wp.img, &trafo );
        pixman_image_composite32(PIXMAN_OP_OVER, wp.img, NULL, pix, 0, 0, 0, 0, 0, 0, w, h );
        // TODO files should be rendered here
    }
    sem_post( &wp.sem );
}

static void wp_init( Wallpaper* wp ) {
    sem_init( &wp->df.sem, 0, 0 );
    sem_init( &wp->sem, 0, 0 );

    glob( "/home/lennart/Wallpapers/""*.png", 0, NULL, &wp->gl );
    wp->files = wp->gl.gl_pathv;
    wp->n_files = wp->gl.gl_pathc;
    wp->show = 1; // everything else 0
    wp->index = -1; // because we do += 1 as first thing

    wp->expiry_time = 100; // number of steps // TODO

    sem_post( &wp->df.sem );
    sem_post( &wp->sem );
    drawroot_update_func( wpfunc );
}

static void wp_destroy( Wallpaper* wp ) {
    sem_wait( &wp->df.sem );
    sem_wait( &wp->sem );
    sem_destroy( &wp->df.sem );
    sem_destroy( &wp->sem );
    globfree( &wp->gl );
}

static void* wp_thread( void* data ) {
    while (1) {
        usleep(2e5);
        wp.current_time++;
        drawroot_trigger(1);
    }
    return NULL;
}
*/

static void awl_plugin_start( awl_plugin_data_t* p ) {
    p->awl_colors = awl_colors();

    p->ip = ip_init();
    p->stats = stats_init( 16, 16, 16 );

    p->temp = calloc(1,sizeof(awl_temperature_t));
    // CPU package sensor (cheap MSR read); the ACPI thermal zone goes through
    // the EC and costs ~2ms per read, so it is only the fallback
    if (temp_find_hwmon( "coretemp", "Package id", p->temp->f_files[p->temp->f_ntemps],
                         sizeof(p->temp->f_files[0]) )) {
        // idles around 50, throttles at TjMax (110)
        p->temp->f_t_max[p->temp->f_ntemps] = 100;
        p->temp->f_t_min[p->temp->f_ntemps] = 50;
    } else {
        strcpy( p->temp->f_files[p->temp->f_ntemps], "/sys/class/thermal/thermal_zone0/temp" );
        p->temp->f_t_max[p->temp->f_ntemps] = 90;
        p->temp->f_t_min[p->temp->f_ntemps] = 40;
    }
    strcpy( p->temp->f_labels[p->temp->f_ntemps++], "" );
    // here could go another thermal zone
    temp_init(p->temp);
    p->temp_color = &temp_color;

    p->bat = bat_init();
    p->date = date_init();
    p->poller = poller_start( p->stats, p->temp, p->date, p->bat, p->ip );
    poller_set_paused( p->poller, p->paused );
    p->pulse = start_pulse_thread();
    p->backlight = start_backlight_thread();
    /*wp_init( &wp );*/
    /*AWL_PTHREAD_CREATE( &p->wp_thread, NULL, wp_thread, NULL );*/
}

static void awl_plugin_stop( awl_plugin_data_t* p ) {
    poller_stop(p->poller);
    ip_free(p->ip);
    stats_free(p->stats);
    temp_fini(p->temp); free(p->temp);
    bat_free(p->bat);
    date_free(p->date);
    stop_pulse_thread(p->pulse);
    stop_backlight_thread(p->backlight);

    /*if (!pthread_cancel( p->wp_thread ))*/
    /*    pthread_join( p->wp_thread, NULL );*/
    /*wp_destroy( &wp );*/
}

/* everything below is the library's side of awl_plugin_abi.h */

const awl_host_t* awl_host = NULL;
static awl_plugin_data_t* plugin_data = NULL;

awl_plugin_data_t* awl_plugin_get( void ) {
    return plugin_data;
}

/* plugins/redraw.c lives in dwl, which owns the eventfd */
void awl_redraw_request( void ) {
    awl_host->redraw_request();
}

static void api_init( int paused ) {
    plugin_data = calloc(1,sizeof(awl_plugin_data_t));
    plugin_data->paused = paused;
    awl_plugin_start( plugin_data );
}

static void api_fini( void ) {
    if (!plugin_data) return;
    awl_plugin_stop( plugin_data );
    free( plugin_data );
    plugin_data = NULL;
}

static void api_set_paused( int paused ) {
    if (!plugin_data) return;
    plugin_data->paused = paused;
    if (plugin_data->poller) poller_set_paused( plugin_data->poller, paused );
}

static const awl_plugin_api_t api = {
    .abi = AWL_PLUGIN_ABI,
    .init = api_init,
    .fini = api_fini,
    .set_paused = api_set_paused,
    .bar_widgets = awl_widgets_create,
};

__attribute__((visibility("default")))
const awl_plugin_api_t* awl_plugin_entry( const awl_host_t* host ) {
    if (!host || host->abi != AWL_PLUGIN_ABI ||
        host->sizeof_host != sizeof(awl_host_t) ||
        host->sizeof_drwl != sizeof(Drwl) ||
        host->sizeof_widget != sizeof(widget_t) ||
        host->sizeof_monitor != sizeof(Monitor) ||
        host->sizeof_client != sizeof(Client))
        return NULL;
    awl_host = host;
    return &api;
}
