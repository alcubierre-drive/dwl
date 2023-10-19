#pragma once

#include <stdint.h>
#include <libinput.h>
#include <xkbcommon/xkbcommon.h>

typedef struct Rule Rule;
typedef struct Layout Layout;
typedef struct MonitorRule MonitorRule;
typedef struct Key Key;
typedef struct Button Button;

#define ARRAY( type, name ) type* name; int n_##name;
typedef struct config_t {
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
} config_t;

