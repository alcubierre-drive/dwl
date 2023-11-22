#pragma once

/*
 * See LICENSE file for copyright and license details.
 */
#include <getopt.h>
#include <libinput.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_input_inhibitor.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include <wlr/xwayland.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>

#include "awl_arg.h"

#define TAGCOUNT (9)
#ifndef AWL_MODKEY
//#define AWL_MODKEY WLR_MODIFIER_LOGO
#define AWL_MODKEY WLR_MODIFIER_ALT
#endif

void awl_change_modkey( uint32_t modkey );
int awl_is_ready( void );

/* macros */
#define CLEANMASK(mask)         (mask & ~WLR_MODIFIER_CAPS)
#define VISIBLEON(C, M)         ((M) && (C)->mon == (M) && ((C)->tags & (M)->tagset[(M)->seltags]))
#define TAGMASK                 ((1u << TAGCOUNT) - 1)
#define LISTEN(E, L, H)         wl_signal_add((E), ((L)->notify = (H), (L)))

/* enums */
enum { CurNormal, CurPressed, CurMove, CurResize }; /* cursor */
enum { XDGShell, LayerShell, X11Managed, X11Unmanaged }; /* client types */
enum { LyrBg, LyrBottom, LyrTile, LyrFloat, LyrTop, LyrFS, LyrOverlay, LyrBlock, NUM_LAYERS }; /* scene layers */
enum { NetWMWindowTypeDialog, NetWMWindowTypeSplash, NetWMWindowTypeToolbar,
    NetWMWindowTypeUtility, NetLast }; /* EWMH atoms */

typedef struct {
    unsigned int mod;
    unsigned int button;
    void (*func)(const Arg *);
    Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client {
    /* Must keep these three elements in this order */
    unsigned int type; /* XDGShell or X11* */
    struct wlr_box geom; /* layout-relative, includes border */
    Monitor *mon;
    struct wlr_scene_tree *scene;
    struct wlr_scene_rect *border[4]; /* top, bottom, left, right */
    struct wlr_scene_tree *scene_surface;
    struct wl_list link;
    struct wl_list flink;
    union {
        struct wlr_xdg_surface *xdg;
        struct wlr_xwayland_surface *xwayland;
    } surface;
    struct wl_listener commit;
    struct wl_listener map;
    struct wl_listener maximize;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener set_title;
    struct wl_listener fullscreen;
    struct wlr_box prev; /* layout-relative, includes border */
    struct wl_listener activate;
    struct wl_listener configure;
    struct wl_listener set_hints;
    unsigned int bw;
    uint32_t tags;

    struct { uint8_t
        visible:1,
        maximized:1,
        isfloating:1,
        isurgent:1,
        isfullscreen:1;
    };

    uint32_t resize; /* configure serial of a pending resize */
} Client;

typedef struct Key {
    uint32_t mod;
    xkb_keysym_t keysym;
    void (*func)(const Arg *);
    Arg arg;
} Key;

typedef struct {
    struct wl_list link;
    struct wlr_keyboard *wlr_keyboard;

    int nsyms;
    const xkb_keysym_t *keysyms; /* invalid if nsyms == 0 */
    uint32_t mods; /* invalid if nsyms == 0 */
    struct wl_event_source *key_repeat_source;

    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
} Keyboard;

typedef struct {
    /* Must keep these three elements in this order */
    unsigned int type; /* LayerShell */
    struct wlr_box geom;
    Monitor *mon;
    struct wlr_scene_tree *scene;
    struct wlr_scene_tree *popups;
    struct wlr_scene_layer_surface_v1 *scene_layer;
    struct wl_list link;
    int mapped;
    struct wlr_layer_surface_v1 *layer_surface;

    struct wl_listener destroy;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener surface_commit;
} LayerSurface;

typedef struct Layout {
    const char *symbol;
    void (*arrange)(Monitor *);
} Layout;

struct Monitor {
    struct wl_list link;
    struct wl_list dwl_ipc_outputs;
    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;
    struct wlr_scene_rect *fullscreen_bg; /* See createmon() for info */
    struct wl_listener frame;
    struct wl_listener destroy;
    struct wl_listener destroy_lock_surface;
    struct wlr_session_lock_surface_v1 *lock_surface;
    struct wlr_box m; /* monitor area, layout-relative */
    struct wlr_box w; /* window area, layout-relative */
    struct wl_list layers[4]; /* LayerSurface::link */
    int n_layers;
    int lt[2];
    unsigned int seltags;
    unsigned int sellt;
    uint32_t tagset[2];
    double mfact;
    int nmaster;
    char ltsymbol[16];
};

typedef struct {
    const char *name;
    float mfact;
    int nmaster;
    float scale;
    int lt;
    /* Layout *lt; */
    enum wl_output_transform rr;
    int x, y;
} MonitorRule;

typedef struct {
    const char *id;
    const char *title;
    uint32_t tags;
    int isfloating;
    int monitor;
} Rule;

typedef struct {
    struct wlr_scene_tree *scene;

    struct wlr_session_lock_v1 *lock;
    struct wl_listener new_surface;
    struct wl_listener unlock;
    struct wl_listener destroy;
} SessionLock;

typedef struct {
    struct wl_list link;
    struct wl_resource *resource;
    Monitor *mon;
} DwlIpcOutput;

void arrange(Monitor *m);
void focusclient(Client *c, int lift);
void focusmon(const Arg *arg);
void focusstack(const Arg *arg);
Client *focustop(Monitor *m);
void killclient(const Arg *arg);
void moveresize(const Arg *arg);
void printstatus(void);
void resize(Client *c, struct wlr_box geo, int interact);
void setlayout(const Arg *arg);
void setmfact(const Arg *arg);
void tag(const Arg *arg);
void tagmon(const Arg *arg);
void togglebar(const Arg *arg);
void togglefloating(const Arg *arg);
void togglefullscreen(const Arg *arg);
void toggletag(const Arg *arg);
void toggleview(const Arg *arg);
void view(const Arg *arg);
void incnmaster(const Arg *arg);

extern xcb_atom_t netatom[NetLast];
