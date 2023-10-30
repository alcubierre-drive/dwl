#pragma once

#include "awl.h"

typedef struct awl_state_t awl_state_t;
typedef struct awl_config_t awl_config_t;

#define ARRAY( type, name ) type* name; int n_##name;

struct awl_config_t {
    int sloppyfocus;
    int bypass_surface_visibility;

    unsigned borderpx;
    float bordercolor[4];
    float focuscolor[4];
    float urgentcolor[4];
    float fullscreen_bg[4];

    int log_level;
    ARRAY( Rule, rules )
    ARRAY( Layout, layouts )
    int cur_layout;
    ARRAY( MonitorRule, monrules )
    struct xkb_rule_names xkb_rules;

    int repeat_rate;
    int repeat_delay;

    int tap_to_click;
    int tap_and_drag;
    int drag_lock;
    int natural_scrolling;
    int disable_while_typing;
    int left_handed;
    int middle_button_emulation;

    enum libinput_config_scroll_method scroll_method;
    enum libinput_config_click_method click_method;
    uint32_t send_events_mode;
    enum libinput_config_accel_profile accel_profile;
    double accel_speed;
    enum libinput_config_tap_button_map button_map;

    ARRAY( Key, keys )
    ARRAY( Button, buttons )

    pthread_t BarThread;
    pthread_t BarRefreshThread;
};

struct awl_state_t {
    char broken[128];
    char cursor_image[1024];
    pid_t child_pid;
    int locked;
    void* exclusive_focus;
    struct wl_display *dpy;
    struct wlr_backend *backend;
    struct wlr_scene *scene;
    struct wlr_scene_tree *layers[NUM_LAYERS];
    struct wlr_scene_tree *drag_icon;
    ARRAY( int, layermap )
    struct wlr_renderer *drw;
    struct wlr_allocator *alloc;
    struct wlr_compositor *compositor;

    struct wlr_xdg_shell *xdg_shell;
    struct wlr_xdg_activation_v1 *activation;
    struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;
    struct wl_list clients; /* tiling order */
    struct wl_list fstack;  /* focus order */
    struct wlr_idle *idle;
    struct wlr_idle_notifier_v1 *idle_notifier;
    struct wlr_idle_inhibit_manager_v1 *idle_inhibit_mgr;
    struct wlr_input_inhibit_manager *input_inhibit_mgr;
    struct wlr_layer_shell_v1 *layer_shell;
    struct wlr_output_manager_v1 *output_mgr;
    struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_mgr;

    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;

    struct wlr_session_lock_manager_v1 *session_lock_mgr;
    struct wlr_scene_rect *locked_bg;
    struct wlr_session_lock_v1 *cur_lock;

    struct wlr_seat *seat;
    struct wl_list keyboards;
    unsigned int cursor_mode;
    Client *grabc;
    int grabcx, grabcy;

    struct wlr_output_layout *output_layout;
    struct wlr_box sgeom;
    struct wl_list mons;
    Monitor *selmon;
};

awl_state_t* awl_state_init( void );
void awl_state_free( awl_state_t* state );
