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
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define END(A)                  ((A) + LENGTH(A))
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
    int lt[2];
    /* Layout *lt[2]; */
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

// needed?
void dwl_ipc_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id);
void dwl_ipc_manager_destroy(struct wl_resource *resource);
void dwl_ipc_manager_get_output(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *output);
void dwl_ipc_manager_release(struct wl_client *client, struct wl_resource *resource);
void dwl_ipc_output_destroy(struct wl_resource *resource);
void dwl_ipc_output_printstatus(Monitor *monitor);
void dwl_ipc_output_printstatus_to(DwlIpcOutput *ipc_output);
void dwl_ipc_output_set_client_tags(struct wl_client *client, struct wl_resource *resource, uint32_t and_tags, uint32_t xor_tags);
void dwl_ipc_output_set_layout(struct wl_client *client, struct wl_resource *resource, uint32_t index);
void dwl_ipc_output_set_tags(struct wl_client *client, struct wl_resource *resource, uint32_t tagmask, uint32_t toggle_tagset);
void dwl_ipc_output_release(struct wl_client *client, struct wl_resource *resource);

/* function declarations */
void applybounds(Client *c, struct wlr_box *bbox);
void applyrules(Client *c);
void arrange(Monitor *m);
void arrangelayer(Monitor *m, struct wl_list *list,
        struct wlr_box *usable_area, int exclusive);
void arrangelayers(Monitor *m);
void axisnotify(struct wl_listener *listener, void *data);
void buttonpress(struct wl_listener *listener, void *data);
void chvt(const Arg *arg);
void checkidleinhibitor(struct wlr_surface *exclude);
void cleanup(void);
void cleanupkeyboard(struct wl_listener *listener, void *data);
void cleanupmon(struct wl_listener *listener, void *data);
void closemon(Monitor *m);
void commitlayersurfacenotify(struct wl_listener *listener, void *data);
void commitnotify(struct wl_listener *listener, void *data);
void createdecoration(struct wl_listener *listener, void *data);
void createidleinhibitor(struct wl_listener *listener, void *data);
void createkeyboard(struct wlr_keyboard *keyboard);
void createlayersurface(struct wl_listener *listener, void *data);
void createlocksurface(struct wl_listener *listener, void *data);
void createmon(struct wl_listener *listener, void *data);
void createnotify(struct wl_listener *listener, void *data);
void createpointer(struct wlr_pointer *pointer);
void cursorframe(struct wl_listener *listener, void *data);
void destroydragicon(struct wl_listener *listener, void *data);
void destroyidleinhibitor(struct wl_listener *listener, void *data);
void destroylayersurfacenotify(struct wl_listener *listener, void *data);
void destroylock(SessionLock *lock, int unlocked);
void destroylocksurface(struct wl_listener *listener, void *data);
void destroynotify(struct wl_listener *listener, void *data);
void destroysessionlock(struct wl_listener *listener, void *data);
void destroysessionmgr(struct wl_listener *listener, void *data);
Monitor *dirtomon(enum wlr_direction dir);
void focusclient(Client *c, int lift);
void focusmon(const Arg *arg);
void focusstack(const Arg *arg);
Client *focustop(Monitor *m);
void fullscreennotify(struct wl_listener *listener, void *data);
void handlesig(int signo);
void incnmaster(const Arg *arg);
void inputdevice(struct wl_listener *listener, void *data);
int keybinding(uint32_t mods, xkb_keysym_t sym);
void keypress(struct wl_listener *listener, void *data);
void keypressmod(struct wl_listener *listener, void *data);
int keyrepeat(void *data);
void killclient(const Arg *arg);
void locksession(struct wl_listener *listener, void *data);
void maplayersurfacenotify(struct wl_listener *listener, void *data);
void mapnotify(struct wl_listener *listener, void *data);
void maximizenotify(struct wl_listener *listener, void *data);
void monocle(Monitor *m);
void motionabsolute(struct wl_listener *listener, void *data);
void motionnotify(uint32_t time);
void motionrelative(struct wl_listener *listener, void *data);
void moveresize(const Arg *arg);
void outputmgrapply(struct wl_listener *listener, void *data);
void outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test);
void outputmgrtest(struct wl_listener *listener, void *data);
void pointerfocus(Client *c, struct wlr_surface *surface,
        double sx, double sy, uint32_t time);
void printstatus(void);
void quit(const Arg *arg);
void rendermon(struct wl_listener *listener, void *data);
void requeststartdrag(struct wl_listener *listener, void *data);
void resize(Client *c, struct wlr_box geo, int interact);
void run(char *startup_cmd);
void setcursor(struct wl_listener *listener, void *data);
void setfloating(Client *c, int floating);
void setfullscreen(Client *c, int fullscreen);
void setlayout(const Arg *arg);
void setmfact(const Arg *arg);
void setmon(Client *c, Monitor *m, uint32_t newtags);
void setpsel(struct wl_listener *listener, void *data);
void setsel(struct wl_listener *listener, void *data);
void setup(void);
void spawn(const Arg *arg);
void startdrag(struct wl_listener *listener, void *data);
void tag(const Arg *arg);
void tagmon(const Arg *arg);
void tile(Monitor *m);
void togglefloating(const Arg *arg);
void togglefullscreen(const Arg *arg);
void toggletag(const Arg *arg);
void toggleview(const Arg *arg);
void togglebar(const Arg *arg);
void unlocksession(struct wl_listener *listener, void *data);
void unmaplayersurfacenotify(struct wl_listener *listener, void *data);
void unmapnotify(struct wl_listener *listener, void *data);
void updatemons(struct wl_listener *listener, void *data);
void updatetitle(struct wl_listener *listener, void *data);
void urgent(struct wl_listener *listener, void *data);
void view(const Arg *arg);
void virtualkeyboard(struct wl_listener *listener, void *data);
Monitor *xytomon(double x, double y);
void xytonode(double x, double y, struct wlr_surface **psurface,
        Client **pc, LayerSurface **pl, double *nx, double *ny);
void zoom(const Arg *arg);

pid_t vfork( void );

/* global event handlers */
extern struct wl_listener cursor_axis;
extern struct wl_listener cursor_button;
extern struct wl_listener cursor_frame;
extern struct wl_listener cursor_motion;
extern struct wl_listener cursor_motion_absolute;
extern struct wl_listener drag_icon_destroy;
extern struct wl_listener idle_inhibitor_create;
extern struct wl_listener idle_inhibitor_destroy;
extern struct wl_listener layout_change;
extern struct wl_listener new_input;
extern struct wl_listener new_virtual_keyboard;
extern struct wl_listener new_output;
extern struct wl_listener new_xdg_surface;
extern struct wl_listener new_xdg_decoration;
extern struct wl_listener new_layer_shell_surface;
extern struct wl_listener output_mgr_apply;
extern struct wl_listener output_mgr_test;
extern struct wl_listener request_activate;
extern struct wl_listener request_cursor;
extern struct wl_listener request_set_psel;
extern struct wl_listener request_set_sel;
extern struct wl_listener request_start_drag;
extern struct wl_listener start_drag;
extern struct wl_listener session_lock_create_lock;
extern struct wl_listener session_lock_mgr_destroy;

void activatex11(struct wl_listener *listener, void *data);
void configurex11(struct wl_listener *listener, void *data);
void createnotifyx11(struct wl_listener *listener, void *data);
xcb_atom_t getatom(xcb_connection_t *xc, const char *name);
void sethints(struct wl_listener *listener, void *data);
void xwaylandready(struct wl_listener *listener, void *data);
extern struct wl_listener new_xwayland_surface;
extern struct wl_listener xwayland_ready;
extern struct wlr_xwayland *xwayland;
extern xcb_atom_t netatom[NetLast];
