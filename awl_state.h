#pragma once

#include "awl.h"

typedef struct awl_state_t awl_state_t;
typedef struct awl_config_t awl_config_t;
typedef struct awl_plugin_data_t awl_plugin_data_t;
typedef struct awl_dbus_listener_t awl_dbus_listener_t;
typedef struct awl_bar_handle_t awl_bar_handle_t;
typedef void (*awl_dbus_hook_t)( const char* signal, void* userdata );
typedef void (*awl_dbus_void_t)( void* userdata );
typedef void (*awl_dbus_ui64_t)( uint64_t signal, void* userdata );

typedef struct LayerSurface LayerSurface;

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

    // functions needed in awl.c, but inherently belonging to awl_plugin/init.c
    void (*setlayout)( const Arg* );
    awl_bar_handle_t* bars;
    awl_plugin_data_t* P;
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

    struct wlr_pointer_constraints_v1 *pointer_constraints;
    struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr;
    struct wlr_pointer_constraint_v1 *active_constraint;

    struct wlr_session_lock_manager_v1 *session_lock_mgr;
    struct wlr_scene_rect *locked_bg;
    struct wlr_session_lock_v1 *cur_lock;

    struct wlr_seat *seat;
    struct wl_list keyboards;
    struct wlr_surface *held_grab;
    unsigned int cursor_mode;
    Client *grabc;
    int grabcx, grabcy;

    struct wlr_output_layout *output_layout;
    struct wlr_box sgeom;
    struct wl_list mons;
    Monitor *selmon;

    void (*arrange)(Monitor*);
    void (*focusclient)(Client*, int);
    Client* (*focustop)(Monitor *m);
    void (*printstatus)(void);
    void (*resize)(Client*, struct wlr_box, int);
    Monitor* (*dirtomon)(enum wlr_direction);
    void (*setfloating)(Client* , int);
    void (*setontop)(Client*, int);
    void (*xytonode)(double, double, struct wlr_surface**, Client**, LayerSurface**, double* , double*);
    void (*dwl_ipc_output_set_layout)(struct wl_client*, struct wl_resource*, uint32_t );
    void (*setmon)(Client*, Monitor*, uint32_t);
    void (*setfullscreen)(Client*, int);
    void (*ipc_send_toggle_vis)( struct wl_resource* );
    void (*ipc_send_vis)( struct wl_resource*, uint32_t );

    sem_t* (*awl_is_ready_sem)( void );
    int (*awl_is_ready)( void );
    void (*awl_change_modkey)( uint32_t );
    void (*log)( const char*, int, const char*, int, const char*, ... );

    awl_dbus_listener_t* dbus;
    void (*dbus_notify)( const char* name, const char* signal );
    void (*dbus_add_callback)( awl_dbus_listener_t* dbus, const char* name,
            awl_dbus_hook_t hook, void* userdata );
    void (*dbus_add_callback_int)( awl_dbus_listener_t* bus, const char* name,
            awl_dbus_ui64_t hook, void* userdata );
    void (*dbus_add_callback_void)( awl_dbus_listener_t* bus, const char* name,
            awl_dbus_void_t hook, void* userdata );
    void (*dbus_remove_callback)( awl_dbus_listener_t* dbus, const char* name );

    void *persistent_plugin_data;
    size_t persistent_plugin_data_nbytes;
};

awl_state_t* awl_state_init( void );
void awl_state_free( awl_state_t* state );
