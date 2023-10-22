#include "awl_state.h"
#include "awl_extension.h"

#define COLOR_SET( C, hex ) \
    { C[0] = ((hex >> 24) & 0xFF) / 255.0f; \
      C[1] = ((hex >> 16) & 0xFF) / 255.0f; \
      C[2] = ((hex >> 8) & 0xFF) / 255.0f; \
      C[3] = (hex & 0xFF) / 255.0f; }
#define COLOR_SETF( C, F0, F1, F2, F3 ) \
    { C[0] = F0; C[1] = F1; C[2] = F2; C[3] = F3; }

#define ARRAY_INIT( type, ary, capacity ) S. ary = (type*)calloc( capacity, sizeof(type) );
#define ARRAY_APPEND( type, ary, ... ) S. ary[S.n_##ary ++] = (type){__VA_ARGS__};

static awl_config_t S = {0};

static void awl_plugin_init(void) {
    S.sloppyfocus = 1;
    S.bypass_surface_visibility = 0;
    S.borderpx = 2;
    COLOR_SET( S.bordercolor, 0x444444ff );
    COLOR_SET( S.focuscolor, 0x005577ff );
    COLOR_SET( S.urgentcolor, 0xff0000ff );
    COLOR_SETF( S.fullscreen_bg, 0.5, 0.5, 0.5, 1.0 );
    S.log_level = WLR_ERROR;

    ARRAY_INIT(Rule, rules, 16);
    ARRAY_APPEND(Rule, rules, "evolution", NULL, 1<<8, 0, -1 );
    ARRAY_APPEND(Rule, rules, "telegram-desktop", NULL, 1<<7, 0, -1 );

    ARRAY_INIT(Layout, layouts, 16);
    ARRAY_APPEND(Layout, layouts, "[]=", tile );

    /* name, mfact, nmaster, scale, layout, rotate/reflect, x, y */
    ARRAY_INIT(MonitorRule, monrules, 16);
    ARRAY_APPEND(MonitorRule, monrules, NULL, 0.5, 1, 1.0, S.layouts, WL_OUTPUT_TRANSFORM_NORMAL, -1, -1);

    S.xkb_rules = (struct xkb_rule_names) {
        .options = NULL,
        .model = "pc105",
        .layout = "de",
        .variant = "nodeadkeys",
    };
    S.repeat_rate = 25;
    S.repeat_delay = 600;

    S.tap_to_click = 1;
    S.tap_and_drag = 1;
    S.drag_lock = 1;
    S.natural_scrolling = 0;
    S.disable_while_typing = 1;
    S.left_handed = 0;
    S.middle_button_emulation = 0;
    /* You can choose between:
    LIBINPUT_CONFIG_SCROLL_NO_SCROLL
    LIBINPUT_CONFIG_SCROLL_2FG
    LIBINPUT_CONFIG_SCROLL_EDGE
    LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN
    */
    S.scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;
    /* You can choose between:
    LIBINPUT_CONFIG_CLICK_METHOD_NONE
    LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS
    LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER
    */
    S.click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
    /* You can choose between:
    LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
    LIBINPUT_CONFIG_SEND_EVENTS_DISABLED
    LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE
    */
    S.send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
    /* You can choose between:
    LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT
    LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE
    */
    S.accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
    S.accel_speed = 0.0;
    /* You can choose between:
    LIBINPUT_CONFIG_TAP_MAP_LRM -- 1/2/3 finger tap maps to left/right/middle
    LIBINPUT_CONFIG_TAP_MAP_LMR -- 1/2/3 finger tap maps to left/middle/right
    */
    S.button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

    ARRAY_INIT(Key, keys, 256);
    ARRAY_APPEND(Key, keys, MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Q, quit, {0});

    ARRAY_INIT(Button, buttons, 16);
    ARRAY_APPEND(Button, buttons, MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove});
    ARRAY_APPEND(Button, buttons, MODKEY, BTN_MIDDLE, togglefloating, {0});
    ARRAY_APPEND(Button, buttons, MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize});

    S.state = NULL;
    /* strcpy( S.broken, "broken" ); */
    /* strcpy( S.cursor_image, "left_ptr" ); */
    /* S.child_pid = -1; */
    /* S.locked = 0; */

    /* ARRAY_INIT(int, layermap, 16); */
    /* ARRAY_APPEND(int, layermap, LyrBg); */
    /* ARRAY_APPEND(int, layermap, LyrBottom); */
    /* ARRAY_APPEND(int, layermap, LyrTop); */
    /* ARRAY_APPEND(int, layermap, LyrOverlay); */
}

static void awl_plugin_free(void) {
    free(S.rules);
    free(S.layouts);
    free(S.monrules);
    free(S.keys);
    free(S.buttons);
    /* free(S.layermap); */

    memset(&S, 0, sizeof(awl_config_t));
}

#if 0
static const Key keys[] = {
    { MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Q, quit,             {0} },
    { MODKEY|WLR_MODIFIER_CTRL,  XKB_KEY_r, extension_reload, {0} },
    { WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_Terminate_Server, quit, {0} },
#define CHVT(n) { WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_XF86Switch_VT_##n, chvt, {.ui = (n)} }
    CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
    CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};

}
#endif

static awl_config_t* awl_plugin_config(void) {
    return &S;
}

awl_vtable_t AWL_VTABLE_SYM = {
    .init = &awl_plugin_init,
    .free = &awl_plugin_free,
    .config = awl_plugin_config,
};
