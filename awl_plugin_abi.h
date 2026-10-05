#pragma once

/* The boundary between dwl and libawlplugins.so, the live-reloadable part of
 * the bar: the plugin threads (plugins.c, plugins/), the bar widgets
 * (widgets.c) and the desktop file list (desktop_panel.c; dwl's desktop.c
 * only places it). dwl dlopen()s the library and looks up a single symbol,
 * AWL_PLUGIN_ENTRY; everything else goes through the two tables below, so
 * the library has no unresolved references into dwl (no -rdynamic).
 *
 * The library still reads dwl's structs directly (Drwl, widget_t, Monitor,
 * Client, Arg), so it has to be built from the same headers as the running
 * dwl. The entry point refuses a host whose ABI version or struct sizes
 * differ from its own. Bump AWL_PLUGIN_ABI whenever either table changes. */

#include <stddef.h>
#include <stdint.h>
#include "drwl.h"

#define AWL_PLUGIN_ABI 2
#define AWL_PLUGIN_ENTRY "awl_plugin_entry"

/* What dwl provides to the library. All functions are main-thread only,
 * except redraw_request, which any thread may call. */
typedef struct awl_host_t {
    uint32_t abi;
    /* layouts the library compiles against */
    size_t sizeof_host, sizeof_drwl, sizeof_widget, sizeof_monitor, sizeof_client;

    void (*redraw_request)( void );

    /* drawing */
    int (*text)( Drwl* drwl, int x, int y, unsigned int w, unsigned int h,
                 unsigned int lpad, const char* text, pixman_color_t fg, pixman_color_t bg );
    unsigned int (*font_getwidth)( Drwl* drwl, const char* text );

    /* actions */
    void (*view)( const Arg* arg );
    void (*toggleview)( const Arg* arg );
    void (*cycle_view)( const Arg* arg );
    void (*cycle_layout)( const Arg* arg );
    void (*focusstack)( const Arg* arg );
    void (*spawn)( const Arg* arg );
    void (*focusclient)( Client* c, int lift );
    void (*arrange)( Monitor* m );
    /* whatever MOD+w is bound to */
    void (*wallpaper_next)( void );

    /* tray (tray/awl_tray.h) */
    uint32_t (*tray_width)( const char* monitor_id );
    void (*tray_set_widget_x)( const char* monitor_id, uint32_t x );
    void (*calendar_toggle)( const char* monitor_id );
    void (*calendar_show)( const char* monitor_id );
    void (*calendar_hide)( void );
} awl_host_t;

/* What the library provides. Main thread only. */
typedef struct awl_plugin_api_t {
    uint32_t abi;
    /* starts the plugin threads */
    void (*init)( int paused );
    /* stops and joins every plugin thread; afterwards no code of the library
     * runs anymore, except through widgets still attached to a bar. Returns
     * nonzero if a thread couldn't be joined (blocked in the kernel, e.g. on
     * a dead mount): it was left running, so the library must stay loaded. */
    int (*fini)( void );
    /* stops/resumes the 1 s polling while no bar is visible */
    void (*set_paused)( int paused );
    /* fills the bar's (empty) widget lists */
    void (*bar_widgets)( Drwl* bar );

    /* The desktop panel; all sizes in device pixels. dwl owns the buffer
     * and the scene node, the library what goes in it. */
    /* changes whenever the panel's contents may have (files, toggles); 0:
     * show nothing. Cheap, called on every redraw request. */
    uint64_t (*desktop_version)( void );
    /* lays the panel out for the usable area avail_w x avail_h with corner
     * radius r; returns its position in that area and its size (0: none) */
    void (*desktop_measure)( Drwl* drw, int avail_w, int avail_h, int r, float scale,
                             int* x, int* y, int* w, int* h );
    /* draws what the last measure laid out into data (premultiplied ARGB,
     * cleared); dwl has prepared drw for drawing into it */
    void (*desktop_draw)( Drwl* drw, uint32_t* data, int stride, int w, int h, int r,
                          float scale );
    /* a click on the bare desktop; returns whether it was taken */
    int (*desktop_click)( int button );
} awl_plugin_api_t;

/* The library's only exported symbol. Returns NULL if the library can't run
 * against this host (ABI or struct layout mismatch). The host table must
 * stay valid as long as the library is loaded. */
typedef const awl_plugin_api_t* (*awl_plugin_entry_t)( const awl_host_t* host );
