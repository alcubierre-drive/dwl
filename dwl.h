#pragma once
/*
 * See LICENSE file for copyright and license details.
 */
#include <errno.h>
#include <getopt.h>
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <libdrm/drm_fourcc.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include <scenefx/types/fx/clipped_region.h>
#include <scenefx/types/wlr_scene.h>
#pragma GCC diagnostic pop

#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/render/allocator.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <xkbcommon/xkbcommon.h>
#ifdef XWAYLAND
#include <wlr/xwayland.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#endif

// stupid SINCE_VERSION
#include "xdg-shell-protocol.h"

/* macros */
#ifndef MAX
#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#endif
#ifndef MIN
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#endif
#define CLEANMASK(mask)         (mask & ~WLR_MODIFIER_CAPS)
#define VISIBLEON(C, M)         ((M) && (C)->mon == (M) && ((C)->tags & (M)->tagset[(M)->seltags]))
#define VISIBLEON_ACTIVE(C, M)  (VISIBLEON(C, M) && (C)->isvisible)
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define END(A)                  ((A) + LENGTH(A))
#define TAGMASK                 ((1u << LENGTH(tags)) - 1)
#define LISTEN(E, L, H)         wl_signal_add((E), ((L)->notify = (H), (L)))
#define LISTEN_STATIC(E, H)     do { struct wl_listener *_l = ecalloc(1, sizeof*_l); _l->notify = (H); wl_signal_add((E), _l); } while (0)
#define TEXTW(mon, text)        (drwl_font_getwidth(mon->drw, text) + mon->lrpad)

/* enums */
enum { SchemeNorm, SchemeSel, SchemeUrg }; /* color schemes */
enum { CurNormal, CurPressed, CurMove, CurResize }; /* cursor */
enum { XDGShell, LayerShell, X11 }; /* client types */
enum { LyrBg, LyrBottom, LyrTile, LyrFloat, LyrTop, LyrFS, LyrOverlay, LyrBlock, NUM_LAYERS }; /* scene layers */
enum { ClkTagBar, ClkLtSymbol, ClkStatus, ClkTitle, ClkClient, ClkRoot }; /* clicks */
#ifdef XWAYLAND
enum { NetWMWindowTypeDialog, NetWMWindowTypeSplash, NetWMWindowTypeToolbar,
	NetWMWindowTypeUtility, NetLast }; /* EWMH atoms */
#endif

typedef union {
	int i;
	uint32_t ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int click;
	unsigned int mod;
	unsigned int button;
	void (*func)(const Arg *);
	const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
	/* Must keep these three elements in this order */
	unsigned int type; /* XDGShell or X11* */
	struct wlr_box geom; /* layout-relative, includes border */
	Monitor *mon;
	struct wlr_scene_tree *scene;
	struct wlr_scene_rect *border[4]; /* top, bottom, left, right */
    struct wlr_scene_blur *blur;
    float one_minus_alpha;
	struct wlr_scene_tree *scene_surface;
	struct wl_list link;
	struct wl_list flink;
	union {
		struct wlr_xdg_surface *xdg;
		struct wlr_xwayland_surface *xwayland;
	} surface;
	struct wlr_xdg_toplevel_decoration_v1 *decoration;
	struct wl_listener commit;
	struct wl_listener map;
	struct wl_listener maximize;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener set_title;
	struct wl_listener fullscreen;
	struct wl_listener set_decoration_mode;
	struct wl_listener destroy_decoration;
	struct wlr_box prev; /* layout-relative, includes border */
	struct wlr_box bounds;
#ifdef XWAYLAND
	struct wl_listener activate;
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener configure;
	struct wl_listener set_hints;
#endif
	unsigned int bw;
	uint32_t tags;
    struct { uint8_t
        isfloating:1,
        isurgent:1,
        isfullscreen:1,
        isvisible:1,
        ismaximized:1,
        isontop:1;
    };
	uint32_t resize; /* configure serial of a pending resize */
};

typedef struct {
	uint32_t mod;
	xkb_keysym_t keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	struct wl_list link;
	struct wlr_keyboard_group *wlr_group;

	int nsyms;
	const xkb_keysym_t *keysyms; /* invalid if nsyms == 0 */
	uint32_t mods; /* invalid if nsyms == 0 */
	struct wl_event_source *key_repeat_source;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
} KeyboardGroup;

typedef struct {
	/* Must keep these three elements in this order */
	unsigned int type; /* LayerShell */
	struct wlr_box geom;
	Monitor *mon;
	struct wlr_scene_tree *scene;
	struct wlr_scene_tree *popups;
	struct wlr_scene_layer_surface_v1 *scene_layer;
	struct wl_list link;
	short mapped, is_notification, is_launcher;
	struct wlr_layer_surface_v1 *layer_surface;

	struct wl_listener destroy;
	struct wl_listener unmap;
	struct wl_listener surface_commit;

    struct wlr_scene_blur* blur;
} LayerSurface;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

typedef struct Buffer Buffer;
typedef struct Drwl Drwl;
struct Monitor {
	struct wl_list link;
	struct wlr_output *wlr_output;
	struct wlr_scene_output *scene_output;
	struct wlr_scene_buffer *scene_buffer; /* bar buffer */
	struct wlr_scene_rect *fullscreen_bg; /* See createmon() for info */
	struct wl_listener frame;
	struct wl_listener destroy;
	struct wl_listener request_state;
	struct wl_listener destroy_lock_surface;
	struct wlr_session_lock_surface_v1 *lock_surface;
	struct wlr_box m; /* monitor area, layout-relative */
	struct {
		int width, height;
		int real_width, real_height; /* non-scaled */
		float scale;
	} b; /* bar area */
	struct wlr_box w; /* window area, layout-relative */
	struct wl_list layers[4]; /* LayerSurface.link */
	const Layout *lt[2];
	unsigned int seltags;
	unsigned int sellt;
	uint32_t tagset[2];
	float mfact;
	int gamma_lut_changed;
	int nmaster;
	uint8_t showbar:1;
    uint8_t closedbar:1;
	char ltsymbol[16];
	int asleep;
	Drwl *drw;
	int lrpad;
    pid_t tray_pid;
};

struct Buffer {
    struct wlr_buffer base;
    size_t stride;
    size_t w, h;
    uint32_t data[];
};

typedef struct {
	const char *name;
	float mfact;
	int nmaster;
	float scale;
	const Layout *lt;
	enum wl_output_transform rr;
	int x, y;
} MonitorRule;

typedef struct {
	struct wlr_pointer_constraint_v1 *constraint;
	struct wl_listener destroy;
} PointerConstraint;

typedef struct {
	const char *id;
	const char *title;
	uint32_t tags;
	short isfloating;
	short monitor;
	int w, h;
    int blur;
    float one_minus_alpha;
} Rule;

typedef struct {
	struct wlr_scene_tree *scene;

	struct wlr_session_lock_v1 *lock;
	struct wl_listener new_surface;
	struct wl_listener unlock;
	struct wl_listener destroy;
} SessionLock;

void cycle_view(const Arg* arg);
void cycle_layout(const Arg* arg);
void view(const Arg* arg);
void toggleview(const Arg* arg);
void focusstack(const Arg *arg);
pid_t spawn_pid(const Arg *arg);
void spawn(const Arg *arg);
void transluce(const Arg *arg);

void arrange(Monitor *m);
void focusclient(Client *c, int lift);

void drawroot_update_func( void (*func)( pixman_image_t* pix, uint64_t op ) );
int drawroot_eventfd( void );
#define drawroot_trigger( arg ) { \
    int fd = drawroot_eventfd(); \
    uint64_t val = arg; \
    if (fd != -1) { \
        write( fd, &val, sizeof(val) ); \
    } \
}

typedef struct awl_plugin_data_t awl_plugin_data_t;
awl_plugin_data_t* awl_plugin_get( void );
