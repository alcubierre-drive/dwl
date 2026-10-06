#pragma once

/* The boundary between dwl and libawlplugins.so, the live-reloadable part of
 * dwl: the plugin threads (plugins.c, plugins/), the bar widgets
 * (widgets.c), the desktop file list (desktop_panel.c; dwl's desktop.c only
 * places it), picking and decoding the wallpaper (plugins/wallpaper.c; dwl's
 * background.c shows it) and the reloadable half of config.h (awl_config.c).
 * dwl dlopen()s the library and looks up a single symbol,
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
#include "tray/awl_tray.h"
#include "plugins/wallpaper.h"

#define AWL_PLUGIN_ABI 13
#define AWL_PLUGIN_ENTRY "awl_plugin_entry"

/* Every dwl function config.h can bind to a key or button. The library binds
 * them through same-named wrappers around awl_host_t.actions. */
#define AWL_ACTIONS( X ) \
    X( changebw ) X( chvt ) X( cycle_layout ) X( cycle_view ) X( focusmon ) \
    X( focusstack ) X( incnmaster ) X( killclient ) X( maximize ) X( minimize ) \
    X( moveresize ) X( movestack ) X( plugin_restart ) X( quit ) X( setlayout ) X( setmfact ) \
    X( spawn ) X( tag ) X( tagmon ) X( togglebar ) X( togglebw ) X( togglefloating ) \
    X( togglefullscreen ) X( toggleontop ) X( toggletag ) X( toggleview ) \
    X( transluce ) X( unminimize ) X( view ) X( wallpaper ) X( wallpapermode )

#define AWL_ACTION_FIELD( name ) void (*name)( const Arg* arg );
typedef struct awl_actions_t {
    AWL_ACTIONS( AWL_ACTION_FIELD )
} awl_actions_t;
#undef AWL_ACTION_FIELD

/* dwl's layouts' arrange functions, likewise */
#define AWL_ARRANGES( X ) X( bstack ) X( gaplessgrid ) X( monocle ) X( tile )

#define AWL_ARRANGE_FIELD( name ) void (*name)( Monitor* m );
typedef struct awl_arranges_t {
    AWL_ARRANGES( AWL_ARRANGE_FIELD )
} awl_arranges_t;
#undef AWL_ARRANGE_FIELD

/* The reloadable half of config.h (see there), as dwl reads it. Built by
 * AWL_CONFIG_TABLE from config.h's names, in the library and, as the
 * fallback while none is loaded, in dwl. Everything points into whoever built
 * it, so dwl copies what it keeps past a reload. */
typedef struct awl_config_t {
    const Key* keys;
    size_t n_keys;
    const Button* buttons;
    size_t n_buttons;
    const Rule* rules;
    size_t n_rules;
    const Layout* layouts; /* at least one */
    size_t n_layouts;
    const MonitorRule* monrules; /* their lt points into layouts */
    size_t n_monrules;
    const uint32_t (*bordercolors)[BorderLast];
    uint32_t modkey;

    int sloppyfocus, bypass_surface_visibility;
    unsigned int borderpx;
    const char* font;
    int fontsize;
    const float *rootcolor, *fullscreen_bg; /* 4 floats each */
    const float* blur;                      /* strength, alpha */
    int blur_notifications, blur_notifications_radius;
    int blur_launcher, blur_launcher_radius;

    const struct xkb_rule_names* xkb_rules;
    int repeat_rate, repeat_delay;
    int tap_to_click, tap_and_drag, drag_lock, natural_scrolling, disable_while_typing,
        left_handed, middle_button_emulation;
    enum libinput_config_scroll_method scroll_method;
    enum libinput_config_click_method click_method;
    uint32_t send_events_mode;
    enum libinput_config_accel_profile accel_profile;
    double accel_speed;
    enum libinput_config_tap_button_map button_map;

    const awl_tray_config_t* tray;
    const WallpaperConfig* wallpaper;
} awl_config_t;

#define AWL_CONFIG_TABLE (awl_config_t){ \
    .keys = keys, .n_keys = LENGTH( keys ), \
    .buttons = buttons, .n_buttons = LENGTH( buttons ), \
    .rules = rules, .n_rules = LENGTH( rules ), \
    .layouts = layouts, .n_layouts = LENGTH( layouts ), \
    .monrules = monrules, .n_monrules = LENGTH( monrules ), \
    .bordercolors = &bordercolors, .modkey = MODKEY, \
    .sloppyfocus = sloppyfocus, .bypass_surface_visibility = bypass_surface_visibility, \
    .borderpx = borderpx, .font = font, .fontsize = fontsize, \
    .rootcolor = rootcolor, .fullscreen_bg = fullscreen_bg, .blur = locked_blur_config, \
    .blur_notifications = blur_notifications, \
    .blur_notifications_radius = blur_notifications_radius, \
    .blur_launcher = blur_launcher, .blur_launcher_radius = blur_launcher_radius, \
    .xkb_rules = &xkb_rules, .repeat_rate = repeat_rate, .repeat_delay = repeat_delay, \
    .tap_to_click = tap_to_click, .tap_and_drag = tap_and_drag, .drag_lock = drag_lock, \
    .natural_scrolling = natural_scrolling, .disable_while_typing = disable_while_typing, \
    .left_handed = left_handed, .middle_button_emulation = middle_button_emulation, \
    .scroll_method = scroll_method, .click_method = click_method, \
    .send_events_mode = send_events_mode, .accel_profile = accel_profile, \
    .accel_speed = accel_speed, .button_map = button_map, \
    .tray = &tray_config, .wallpaper = &wallpaper_config, \
}

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
    const awl_actions_t* actions;
    const awl_arranges_t* arranges;
    void (*focusclient)( Client* c, int lift );
    void (*arrange)( Monitor* m );

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
    /* a click on the bare desktop, mods as WLR_MODIFIER_* without the ignored
     * ones; returns whether it was taken */
    int (*desktop_click)( int button, uint32_t mods );

    /* changes the wallpaper (plugins/wallpaper.h); returns at once */
    void (*wallpaper)( WallpaperMode mode );
    /* the newest decoded wallpaper not taken yet, or NULL; the caller owns
     * it. A redraw request announces one. */
    awl_image_t* (*wallpaper_take)( void );
    /* the wallpaper timer's mode changed (the wallpapermode action) */
    void (*wallpaper_mode)( WallpaperMode mode );

    /* the reloadable half of config.h; valid until the library is unloaded */
    const awl_config_t* (*config)( void );
} awl_plugin_api_t;

/* The library's only exported symbol. Returns NULL if the library can't run
 * against this host (ABI or struct layout mismatch). The host table must
 * stay valid as long as the library is loaded. */
typedef const awl_plugin_api_t* (*awl_plugin_entry_t)( const awl_host_t* host );
