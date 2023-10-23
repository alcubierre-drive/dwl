#include "../awl_state.h"
#include "../awl_extension.h"
#include "../awl_bar/awl_bar.h"

#define COLOR_SET( C, hex ) \
    { C[0] = ((hex >> 24) & 0xFF) / 255.0f; \
      C[1] = ((hex >> 16) & 0xFF) / 255.0f; \
      C[2] = ((hex >> 8) & 0xFF) / 255.0f; \
      C[3] = (hex & 0xFF) / 255.0f; }
#define COLOR_SETF( C, F0, F1, F2, F3 ) \
    { C[0] = F0; C[1] = F1; C[2] = F2; C[3] = F3; }

#define ARRAY_INIT( type, ary, capacity ) S. ary = (type*)calloc( capacity, sizeof(type) );
#define ARRAY_APPEND( type, ary, ... ) S. ary[S.n_##ary ++] = (type){__VA_ARGS__};

extern awl_vtable_t AWL_VTABLE_SYM;
static awl_config_t S = {0};

static void cycle_tag( const Arg* arg );
static void cycle_layout( const Arg* arg );

static void awl_plugin_init(void) {
    S.sloppyfocus = 1;
    S.bypass_surface_visibility = 0;
    S.borderpx = 2;
    COLOR_SET( S.bordercolor, 0x444444ff );
    COLOR_SET( S.focuscolor, 0x005577ff );
    COLOR_SET( S.urgentcolor, 0xff0000ff );
    COLOR_SETF( S.fullscreen_bg, 0.5, 0.5, 0.5, 1.0 );

    ARRAY_INIT(Rule, rules, 16);
    ARRAY_APPEND(Rule, rules, "evolution", NULL, 1<<8, 0, -1 );
    ARRAY_APPEND(Rule, rules, "telegram-desktop", NULL, 1<<7, 0, -1 );

    ARRAY_INIT(Layout, layouts, 16);
    ARRAY_APPEND(Layout, layouts, "[]=", tile );
    ARRAY_APPEND(Layout, layouts, "><>", NULL );
    ARRAY_APPEND(Layout, layouts, "[M]", monocle );
    S.cur_layout = 0;

    /* name, mfact, nmaster, scale, layout, rotate/reflect, x, y */
    ARRAY_INIT(MonitorRule, monrules, 16);
    ARRAY_APPEND(MonitorRule, monrules, NULL, 0.5, 1, 1.0, 0, WL_OUTPUT_TRANSFORM_NORMAL, -1, -1);

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

    // Key combinations
    ARRAY_INIT(Key, keys, 256);

    #define MODKEY_SH MODKEY|WLR_MODIFIER_SHIFT
    #define MODKEY_CT MODKEY|WLR_MODIFIER_CTRL
    #define MODKEY_CT_SH MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_SHIFT

    #define ADD_KEY( MOD, KEY, FUN, ARG ) ARRAY_APPEND(Key, keys, MOD, KEY, FUN, ARG);
    #define ADD_TAG( KEY, SKEY, TAG) \
    ADD_KEY( MODKEY,       KEY,  view,       {.ui = 1 << TAG} ) \
    ADD_KEY( MODKEY_CT,    KEY,  toggleview, {.ui = 1 << TAG} ) \
    ADD_KEY( MODKEY_SH,    SKEY, tag,        {.ui = 1 << TAG} ) \
    ADD_KEY( MODKEY_CT_SH, SKEY, toggletag,  {.ui = 1 << TAG} )

    /* static const char *termcmd[] = {"kitty", "--single-instance", NULL}; */
    static const char *termcmd[] = {"kitty", NULL};
    static const char *menucmd[] = {"bemenu-run", NULL};

    ADD_KEY( MODKEY,    XKB_KEY_p,         spawn,           {.v=menucmd} )
    ADD_KEY( MODKEY,    XKB_KEY_Return,    spawn,           {.v=termcmd} )
    ADD_KEY( MODKEY,    XKB_KEY_j,         focusstack,      {.i = +1} )
    ADD_KEY( MODKEY,    XKB_KEY_k,         focusstack,      {.i = -1} )
    ADD_KEY( MODKEY,    XKB_KEY_i,         incnmaster,      {.i = +1} )
    ADD_KEY( MODKEY,    XKB_KEY_d,         incnmaster,      {.i = -1} )
    ADD_KEY( MODKEY,    XKB_KEY_h,         setmfact,        {.f = -0.05} )
    ADD_KEY( MODKEY,    XKB_KEY_l,         setmfact,        {.f = +0.05} )
    ADD_KEY( MODKEY_SH, XKB_KEY_Return,    zoom,            {0} )
    ADD_KEY( MODKEY,    XKB_KEY_Tab,       view,            {0} )
    ADD_KEY( MODKEY_SH, XKB_KEY_C,         killclient,      {0} )
    ADD_KEY( MODKEY_CT, XKB_KEY_space,     togglefloating,  {0} )
    ADD_KEY( MODKEY,    XKB_KEY_f,         togglefullscreen,{0} )
    ADD_KEY( MODKEY,    XKB_KEY_0,         view,            {.ui = ~0} )
    ADD_KEY( MODKEY_SH, XKB_KEY_equal,     tag,             {.ui = ~0} )
    ADD_KEY( MODKEY,    XKB_KEY_comma,     focusmon,        {.i = WLR_DIRECTION_LEFT} )
    ADD_KEY( MODKEY,    XKB_KEY_period,    focusmon,        {.i = WLR_DIRECTION_RIGHT} )
    ADD_KEY( MODKEY_SH, XKB_KEY_semicolon, tagmon,          {.i = WLR_DIRECTION_LEFT} )
    ADD_KEY( MODKEY_SH, XKB_KEY_colon,     tagmon,          {.i = WLR_DIRECTION_RIGHT} )

    ADD_KEY( MODKEY, XKB_KEY_Right, cycle_tag,    {.i= 1} )
    ADD_KEY( MODKEY, XKB_KEY_Left,  cycle_tag,    {.i=-1} )
    ADD_KEY( MODKEY, XKB_KEY_space, cycle_layout, {0} )

    ADD_TAG( XKB_KEY_1, XKB_KEY_exclam,     0)
    ADD_TAG( XKB_KEY_2, XKB_KEY_quotedbl,   1)
    ADD_TAG( XKB_KEY_3, XKB_KEY_section,    2)
    ADD_TAG( XKB_KEY_4, XKB_KEY_dollar,     3)
    ADD_TAG( XKB_KEY_5, XKB_KEY_percent,    4)
    ADD_TAG( XKB_KEY_6, XKB_KEY_ampersand,  5)
    ADD_TAG( XKB_KEY_7, XKB_KEY_slash,      6)
    ADD_TAG( XKB_KEY_8, XKB_KEY_parenleft,  7)
    ADD_TAG( XKB_KEY_9, XKB_KEY_parenright, 8)


    ARRAY_INIT(Button, buttons, 16);
    ARRAY_APPEND(Button, buttons, MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove});
    ARRAY_APPEND(Button, buttons, MODKEY, BTN_MIDDLE, togglefloating, {0});
    ARRAY_APPEND(Button, buttons, MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize});

    pthread_create( &S.BarThread, NULL, dwlb, NULL );
}

static void awl_plugin_free(void) {
    free(S.rules);
    free(S.layouts);
    free(S.monrules);
    free(S.keys);
    free(S.buttons);

    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (B) {
        dwlb_run_display = false;
        togglebar(NULL);
        pthread_join( S.BarThread, NULL );
    }

    memset(&S, 0, sizeof(awl_config_t));
}

static void cycle_tag( const Arg* arg ) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    Monitor* selmon = B->selmon;
    if (!selmon) return;

    uint32_t c_tag = selmon->tagset[selmon->seltags] & TAGMASK;
    int32_t c_count = 0;
    while (c_tag > 1) { c_tag/=2; c_count++; };

    selmon->seltags ^= 1; /* toggle sel tagset */
    if (arg->ui & TAGMASK)
        selmon->tagset[selmon->seltags] = (1 << ((arg->i + TAGCOUNT + c_count)%TAGCOUNT)) & TAGMASK;
    focusclient(focustop(selmon), 1);
    arrange(selmon);
    printstatus();
}

static void cycle_layout( const Arg* arg ) {
    (void)arg;
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    S.cur_layout++;
    S.cur_layout %= S.n_layouts;

    Arg A = {.i = S.cur_layout};
    setlayout( &A );
    focusclient(focustop(B->selmon), 1);
}

awl_vtable_t AWL_VTABLE_SYM = {
    .init = &awl_plugin_init,
    .free = &awl_plugin_free,
    .config = &S,
    .state = NULL,
};
