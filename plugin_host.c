/* memfd_create() */
#define _GNU_SOURCE

#include "plugin_host.h"
#include "awl_plugin_abi.h"
#include "plugins/redraw.h"
#include "tray/awl_tray.h"

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static const awl_host_t host = {
    .abi = AWL_PLUGIN_ABI,
    .sizeof_host = sizeof(awl_host_t),
    .sizeof_drwl = sizeof(Drwl),
    .sizeof_widget = sizeof(widget_t),
    .sizeof_monitor = sizeof(Monitor),
    .sizeof_client = sizeof(Client),

    .redraw_request = awl_redraw_request,

    .text = drwl_text_color2,
    .font_getwidth = drwl_font_getwidth,

    .view = view,
    .toggleview = toggleview,
    .cycle_view = cycle_view,
    .cycle_layout = cycle_layout,
    .focusstack = focusstack,
    .spawn = spawn,
    .focusclient = focusclient,
    .arrange = arrange,

    .tray_width = awl_tray_width,
    .tray_set_widget_x = awl_tray_set_widget_x,
    .calendar_toggle = awl_tray_calendar_toggle,
    .calendar_show = awl_tray_calendar_show,
    .calendar_hide = awl_tray_calendar_hide,
};

static void* handle = NULL;
static const awl_plugin_api_t* api = NULL;
static int paused = 0;

/* $AWL_PLUGINS, or libawlplugins.so next to the dwl binary */
static int libpath( char* out, size_t n ) {
    const char* env = getenv( "AWL_PLUGINS" );
    if (env && *env)
        return snprintf( out, n, "%s", env ) < (int)n ? 0 : -1;

    char exe[PATH_MAX];
    ssize_t len = readlink( "/proc/self/exe", exe, sizeof(exe)-1 );
    if (len < 0) return -1;
    exe[len] = 0;
    char* slash = strrchr( exe, '/' );
    if (slash) *slash = 0;
    return snprintf( out, n, "%s/libawlplugins.so", exe ) < (int)n ? 0 : -1;
}

/* Copies the file at path into a memfd, so the loader sees a new file every
 * time: dlopen() hands back the already loaded copy for the same file, and a
 * build overwriting the mapped file in place would crash dwl. */
static int memfd_copy( const char* path ) {
    int src = open( path, O_RDONLY | O_CLOEXEC );
    if (src < 0) return -1;
    int dst = memfd_create( "libawlplugins.so", MFD_CLOEXEC );
    if (dst < 0) { close( src ); return -1; }

    char buf[1 << 16];
    ssize_t r;
    while ((r = read( src, buf, sizeof(buf) )) > 0) {
        for (ssize_t off = 0; off < r; ) {
            ssize_t w = write( dst, buf + off, r - off );
            if (w < 0) { r = -1; goto out; }
            off += w;
        }
    }
out:
    close( src );
    if (r < 0) { close( dst ); return -1; }
    return dst;
}

static const awl_plugin_api_t* libopen( void** out ) {
    char path[PATH_MAX], fdpath[64];
    if (libpath( path, sizeof(path) )) {
        fprintf( stderr, "awl plugins: can't determine the library path\n" );
        return NULL;
    }
    int fd = memfd_copy( path );
    if (fd < 0) {
        fprintf( stderr, "awl plugins: can't read %s: %s\n", path, strerror( errno ) );
        return NULL;
    }
    snprintf( fdpath, sizeof(fdpath), "/proc/self/fd/%d", fd );
    void* h = dlopen( fdpath, RTLD_NOW | RTLD_LOCAL );
    close( fd ); // the mapping stays
    if (!h) {
        fprintf( stderr, "awl plugins: %s: %s\n", path, dlerror() );
        return NULL;
    }

    // ISO C has no cast from object to function pointer
    void* sym = dlsym( h, AWL_PLUGIN_ENTRY );
    awl_plugin_entry_t entry;
    memcpy( &entry, &sym, sizeof(entry) );
    const awl_plugin_api_t* a = entry ? entry( &host ) : NULL;
    if (!a || a->abi != AWL_PLUGIN_ABI) {
        fprintf( stderr, "awl plugins: %s: %s\n", path, !entry ? "no entry point" :
                 "built against different headers than this dwl (rebuild both)" );
        dlclose( h );
        return NULL;
    }
    *out = h;
    return a;
}

int awl_plugins_load( int p ) {
    paused = p;
    if (!(api = libopen( &handle )))
        return -1;
    api->init( paused );
    return 0;
}

int awl_plugins_reload( void (*detach)( void ), void (*attach)( void ) ) {
    void* h = NULL;
    const awl_plugin_api_t* next = libopen( &h );

    // nothing may point into the old library past this
    detach();
    if (api) api->fini();
    if (next) {
        if (handle) dlclose( handle );
        handle = h;
        api = next;
    }
    if (api) api->init( paused );
    attach();
    return next ? 0 : -1;
}

void awl_plugins_unload( void ) {
    if (api) api->fini();
    if (handle) dlclose( handle );
    api = NULL;
    handle = NULL;
}

void awl_plugins_set_paused( int p ) {
    paused = p;
    if (api) api->set_paused( paused );
}

void awl_plugins_bar_widgets( Drwl* bar ) {
    if (api) api->bar_widgets( bar );
}
