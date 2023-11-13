#include "init.h"
#include "../awl_log.h"
#include "../awl_util.h"
#include "bar.h"
#include "date.h"
#include "stats.h"
#include "pulsetest.h"
#include "temp.h"
#include "wallpaper.h"

static awl_config_t S = {0};

awl_config_t* awl_plugin_config( void ) {
    return &S;
}

awl_state_t* awl_plugin_state( void ) {
    return AWL_VTABLE_SYM.state;
}

awl_plugin_data_t* awl_plugin_data( void ) {
    return S.P;
}

#define COLOR_SET( C, hex ) \
    { C[0] = ((hex >> 24) & 0xFF) / 255.0f; \
      C[1] = ((hex >> 16) & 0xFF) / 255.0f; \
      C[2] = ((hex >> 8) & 0xFF) / 255.0f; \
      C[3] = (hex & 0xFF) / 255.0f; }
#define COLOR_SETF( C, F0, F1, F2, F3 ) \
    { C[0] = F0; C[1] = F1; C[2] = F2; C[3] = F3; }

#define ARRAY_INIT( type, ary, capacity ) S. ary = (type*)ecalloc( capacity, sizeof(type) );
#define ARRAY_APPEND( type, ary, ... ) S. ary[S.n_##ary ++] = (type){__VA_ARGS__};

static void movestack( const Arg *arg );
static void client_hide( const Arg* arg );
static void tagmon_f( const Arg* arg );
static void bordertoggle( const Arg* arg );
static void cycle_tag( const Arg* arg );
static void cycle_layout( const Arg* arg );

static void gaplessgrid(Monitor *m);
static void bstack(Monitor *m);
static void dwindle(Monitor *mon);

static void setup_bar_colors( void ) {
    pixman_color_t c16 = {0};
    // tag colors
    barcolors.bg_tags = color_8bit_to_16bit( molokai_dark_gray );
    c16 = color_8bit_to_16bit( molokai_orange );
    c16.alpha = 0x7777;
    barcolors.bg_tags_occ = c16;
    c16 = color_8bit_to_16bit( molokai_purple );
    c16.alpha = 0x7777;
    barcolors.bg_tags_act = c16;
    c16 = color_8bit_to_16bit( molokai_red );
    c16.alpha = 0x7777;
    barcolors.bg_tags_urg = c16;
    c16 = color_8bit_to_16bit( molokai_purple );
    c16.alpha = 0x2222;
    barcolors.fg_tags = alpha_blend_16( white, c16 );

    // status/layout colors
    barcolors.bg_status = barcolors.bg_lay = color_8bit_to_16bit( molokai_light_gray );
    barcolors.fg_status = barcolors.fg_lay = barcolors.fg_tags;

    // window colors
    c16 = color_8bit_to_16bit( molokai_purple );
    c16.alpha = 0x4444;
    barcolors.bg_win = alpha_blend_16( color_8bit_to_16bit(molokai_dark_gray), c16 );
    c16.alpha = 0x9999;
    barcolors.bg_win_act = alpha_blend_16( color_8bit_to_16bit(molokai_dark_gray), c16 );
    barcolors.bg_win_urg = barcolors.bg_tags_occ;
    barcolors.bg_win_min = barcolors.bg_tags;
    barcolors.fg_win = barcolors.fg_tags;

    // widget colors
    barcolors.bg_stats = barcolors.bg_tags;
    barcolors.fg_stats_cpu = color_8bit_to_16bit( molokai_blue );
    barcolors.fg_stats_mem = color_8bit_to_16bit( molokai_orange );
    barcolors.fg_stats_swp = color_8bit_to_16bit( molokai_green );

}

static void awl_plugin_init(void) {

    awl_log_printf("setting up environment");
    setenv("MOZ_ENABLE_WAYLAND", "1", 1);
    setenv("QT_STYLE_OVERRIDE","kvantum",1);
    setenv("DESKTOP_SESSION","gnome",1);
    setenv("QT_AUTO_SCREEN_SCALE_FACTOR","0",1);
    setenv("EDITOR","nvim",1);
    setenv("SYSTEMD_EDITOR","/usr/bin/nvim",1);
    setenv("SSH_AUTH_SOCK","1",1);
    setenv("NO_AT_BRIDGE","1",1);

    awl_log_printf( "general setup" );
    S.sloppyfocus = 1;
    S.bypass_surface_visibility = 0;
    S.borderpx = 2;

    awl_log_printf( "color setup" );
    COLOR_SET( S.bordercolor, molokai_light_gray );
    COLOR_SET( S.focuscolor, molokai_blue );
    COLOR_SET( S.urgentcolor, molokai_orange );
    COLOR_SET( S.fullscreen_bg, 0x00000000 );
    setup_bar_colors();

    /* id, title, tags, isfloating, monitor */
    ARRAY_INIT(Rule, rules, 32);
    // tag rules
    ARRAY_APPEND(Rule, rules, "evolution", NULL, 1<<8, 0, -1 );
    ARRAY_APPEND(Rule, rules, "telegram", NULL, 1<<7, 0, -1 );
    // floating rules
    ARRAY_APPEND(Rule, rules, "nomacs", NULL, 0, 1, -1 );
    ARRAY_APPEND(Rule, rules, "python3", "Figure", 0, 1, -1 );
    ARRAY_APPEND(Rule, rules, "wdisplays", NULL, 0, 1, -1 );
    // TODO tray application (RUST)
    ARRAY_APPEND(Rule, rules, "gtk-tray", "AWL", 0, 1, -1 );
    awl_log_printf( "created %i rules", S.n_rules );

    ARRAY_INIT(Layout, layouts, 16);
    ARRAY_APPEND(Layout, layouts, "[◻]", gaplessgrid );
    ARRAY_APPEND(Layout, layouts, "[M]", monocle );
    ARRAY_APPEND(Layout, layouts, "[=]", bstack );
    ARRAY_APPEND(Layout, layouts, "[T]", tile );
    ARRAY_APPEND(Layout, layouts, "[@]", dwindle );
    S.cur_layout = 0;
    awl_log_printf( "created %i layouts", S.n_layouts );

    /* name, mfact, nmaster, scale, layout, rotate/reflect, x, y */
    ARRAY_INIT(MonitorRule, monrules, 16);
    ARRAY_APPEND(MonitorRule, monrules, NULL, 0.5, 1, 1.0, 0, WL_OUTPUT_TRANSFORM_NORMAL, -1, -1);
    awl_log_printf( "created %i monitor rules", S.n_monrules );

    awl_log_printf( "keyboard/mouse config" );
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
    S.scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;
    S.click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
    S.send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
    S.accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
    S.accel_speed = 0.0;
    /* You can choose between:
    LIBINPUT_CONFIG_TAP_MAP_LRM -- 1/2/3 finger tap maps to left/right/middle
    LIBINPUT_CONFIG_TAP_MAP_LMR -- 1/2/3 finger tap maps to left/middle/right
    */
    S.button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

    // in order to change the modkey for the very few default bindings
    // awl_change_modkey(WLR_MODIFIER_ALT);

    // Key combinations
    ARRAY_INIT(Key, keys, 256);

    #define MODKEY AWL_MODKEY
    #define MODKEY_SH AWL_MODKEY|WLR_MODIFIER_SHIFT
    #define MODKEY_CT AWL_MODKEY|WLR_MODIFIER_CTRL
    #define MODKEY_CT_SH AWL_MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_SHIFT
    #define ADD_KEY( MOD, KEY, FUN, ARG ) \
        ARRAY_APPEND(Key, keys, MOD, KEY, FUN, ARG);
    #define ADD_TAG( KEY, SKEY, TAG) \
        ADD_KEY( MODKEY,       KEY,  view,       {.ui = 1 << TAG} ) \
        ADD_KEY( MODKEY_CT,    KEY,  toggleview, {.ui = 1 << TAG} ) \
        ADD_KEY( MODKEY_SH,    SKEY, tag,        {.ui = 1 << TAG} ) \
        ADD_KEY( MODKEY_CT_SH, SKEY, toggletag,  {.ui = 1 << TAG} )
    #ifndef AWL_TERM_CMD
    #define AWL_TERM_CMD "kitty"
    #endif
    #ifndef AWL_MENU_CMD
    #define AWL_MENU_CMD "bemenu-run"
    #endif

    static const char *termcmd[] = {AWL_TERM_CMD, NULL};
    static const char *menucmd[] = {AWL_MENU_CMD, NULL};
    static const char *brightness_m_cmd[] = {"backlight-tooler", "-m", "dec", "-V", "0.05", NULL};
    static const char *brightness_p_cmd[] = {"backlight-tooler", "-m", "inc", "-V", "0.05", NULL};
    static const char *vol_p_cmd[] = {"pactl", "set-sink-volume", "@DEFAULT_SINK@", "+5%", NULL};
    static const char *vol_m_cmd[] = {"pactl", "set-sink-volume", "@DEFAULT_SINK@", "-5%", NULL};
    static const char *vol_mute_cmd[] = {"pactl", "set-sink-mute", "@DEFAULT_SINK@", "toggle", NULL};
    static const char *mic_mute_cmd[] = {"pactl", "set-source-mute", "@DEFAULT_SOURCE@", "toggle", NULL};
    static const char *vol_switch_cmd[] = {"pulse_port_switch", "-t", "-N", NULL};
    static const char *screenshot_cmd[] = {"grim", NULL};
    static const char *display_cmd[] = {"wdisplays", NULL};

    ADD_KEY( MODKEY,    XKB_KEY_p,          spawn,              {.v=menucmd} )
    ADD_KEY( MODKEY,    XKB_KEY_Return,     spawn,              {.v=termcmd} )
    ADD_KEY( MODKEY,    XKB_KEY_j,          focusstack,         {.i = +1} )
    ADD_KEY( MODKEY,    XKB_KEY_k,          focusstack,         {.i = -1} )
    /* ADD_KEY( MODKEY,    XKB_KEY_i,          incnmaster,         {.i = +1} ) */
    /* ADD_KEY( MODKEY,    XKB_KEY_d,          incnmaster,         {.i = -1} ) */
    ADD_KEY( MODKEY,    XKB_KEY_h,          setmfact,           {.f = -0.05} )
    ADD_KEY( MODKEY,    XKB_KEY_l,          setmfact,           {.f = +0.05} )
    ADD_KEY( MODKEY_SH, XKB_KEY_Return,     zoom,               {0} )
    ADD_KEY( MODKEY,    XKB_KEY_Tab,        view,               {0} )
    ADD_KEY( MODKEY_SH, XKB_KEY_C,          killclient,         {0} )
    ADD_KEY( MODKEY_CT, XKB_KEY_space,      togglefloating,     {0} )
    ADD_KEY( MODKEY,    XKB_KEY_f,          togglefullscreen,   {0} )
    ADD_KEY( MODKEY_CT, XKB_KEY_j,          focusmon,           {.i = WLR_DIRECTION_LEFT} )
    ADD_KEY( MODKEY_CT, XKB_KEY_k,          focusmon,           {.i = WLR_DIRECTION_RIGHT} )
    /* ADD_KEY( MODKEY,    XKB_KEY_comma,      focusmon,           {.i = WLR_DIRECTION_LEFT} ) */
    /* ADD_KEY( MODKEY,    XKB_KEY_period,     focusmon,           {.i = WLR_DIRECTION_RIGHT} ) */
    /* ADD_KEY( MODKEY_SH, XKB_KEY_semicolon,  tagmon,             {.i = WLR_DIRECTION_LEFT} ) */
    /* ADD_KEY( MODKEY_SH, XKB_KEY_colon,      tagmon,             {.i = WLR_DIRECTION_RIGHT} ) */

    ADD_KEY( MODKEY_SH,    XKB_KEY_O,       tagmon_f,           {.i = WLR_DIRECTION_LEFT} )
    ADD_KEY( MODKEY_CT_SH, XKB_KEY_O,       tagmon_f,           {.i = WLR_DIRECTION_RIGHT} )

    ADD_KEY( MODKEY,    XKB_KEY_i,          togglebar,          {0} )
    ADD_KEY( MODKEY,    XKB_KEY_n,          client_hide,        {.ui=1} )
    ADD_KEY( MODKEY_CT, XKB_KEY_n,          client_hide,        {.ui=0} )

    ADD_KEY( MODKEY,    XKB_KEY_Right,      cycle_tag,          {.i= 1} )
    ADD_KEY( MODKEY,    XKB_KEY_Left,       cycle_tag,          {.i=-1} )
    ADD_KEY( MODKEY,    XKB_KEY_space,      cycle_layout,       {0} )
    ADD_KEY( MODKEY_SH, XKB_KEY_J,          movestack,          {.i = +1} )
    ADD_KEY( MODKEY_SH, XKB_KEY_K,          movestack,          {.i = -1} )

    ADD_KEY( MODKEY,    XKB_KEY_b,          bordertoggle,       {0} )

    ADD_KEY( 0, XKB_KEY_XF86MonBrightnessUp,    spawn,          {.v=brightness_p_cmd} )
    ADD_KEY( 0, XKB_KEY_XF86MonBrightnessDown,  spawn,          {.v=brightness_m_cmd} )
    ADD_KEY( 0, XKB_KEY_XF86AudioRaiseVolume,   spawn,          {.v=vol_p_cmd} )
    ADD_KEY( 0, XKB_KEY_XF86AudioLowerVolume,   spawn,          {.v=vol_m_cmd} )
    ADD_KEY( 0, XKB_KEY_XF86AudioMute,          spawn,          {.v=vol_mute_cmd} )
    ADD_KEY( 0, XKB_KEY_XF86AudioMicMute,       spawn,          {.v=mic_mute_cmd} )
    ADD_KEY( 0, XKB_KEY_Print,                  spawn,          {.v=screenshot_cmd} );
    ADD_KEY( MODKEY, XKB_KEY_F1,                spawn,          {.v=vol_switch_cmd} )
    ADD_KEY( MODKEY, XKB_KEY_d,                 spawn,          {.v=display_cmd} );
    ADD_KEY( 0, XKB_KEY_XF86Display,            spawn,          {.v=display_cmd} );
    // TODO missing
    /* "systemctl --user stop backlight-tooler.timer; backlight-tooler -m toggle" */
    /*   XF86Favorites */
    /* "backlight-tooler -m auto" */
    /*   XF86Launch1 */
    /* "backlight-tooler-service-toggle" */
    /*   XF86Launch2 */
    /* "bluetooth toggle" */
    /*   XF86Launch3 */
    ADD_TAG( XKB_KEY_1, XKB_KEY_exclam,     0)
    ADD_TAG( XKB_KEY_2, XKB_KEY_quotedbl,   1)
    ADD_TAG( XKB_KEY_3, XKB_KEY_section,    2)
    ADD_TAG( XKB_KEY_4, XKB_KEY_dollar,     3)
    ADD_TAG( XKB_KEY_5, XKB_KEY_percent,    4)
    ADD_TAG( XKB_KEY_6, XKB_KEY_ampersand,  5)
    ADD_TAG( XKB_KEY_7, XKB_KEY_slash,      6)
    ADD_TAG( XKB_KEY_8, XKB_KEY_parenleft,  7)
    ADD_TAG( XKB_KEY_9, XKB_KEY_parenright, 8)
    awl_log_printf( "created %i keybindings", S.n_keys );

    ARRAY_INIT(Button, buttons, 16);
    ARRAY_APPEND(Button, buttons, MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove});
    ARRAY_APPEND(Button, buttons, MODKEY, BTN_MIDDLE, togglefloating, {0});
    ARRAY_APPEND(Button, buttons, MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize});
    awl_log_printf( "created %i mousebindings", S.n_buttons );

    awl_plugin_data_t* P = ecalloc(1, sizeof(awl_plugin_data_t));
    P->refresh_sec = 0.2;

    P->ncpu = P->nmem = P->nswp = 32;
    start_stats_thread( P->cpu, P->ncpu, P->mem, P->nmem, P->swp, P->nswp, 1 );
    P->stats_dir = 0;
    P->date = start_date_thread( 10 );
    P->pulse = start_pulse_thread();
    start_ip_thread( &P->ip, 1 );
    start_bat_thread( &P->bat, 1 );
    #ifndef AWL_CPU_THERMAL_ZONE
    strcpy( P->temp.f_files[P->temp.f_ntemps], "/sys/class/thermal/thermal_zone7/temp" );
    #else // AWL_CPU_THERMAL_ZONE
    strcpy( P->temp.f_files[P->temp.f_ntemps], "/sys/class/thermal/" AWL_CPU_THERMAL_ZONE "/temp" );
    #endif // AWL_CPU_THERMAL_ZONE
    #ifndef AWL_CPU_THERMAL_ZONE_NAME
    strcpy( P->temp.f_labels[P->temp.f_ntemps], "CPU" );
    #else
    strcpy( P->temp.f_labels[P->temp.f_ntemps], AWL_CPU_THERMAL_ZONE_NAME );
    #endif
    P->temp.f_t_max[P->temp.f_ntemps] = 80;
    P->temp.f_t_min[P->temp.f_ntemps++] = 40;
    #ifdef AWL_GPU_THERMAL_ZONE
    strcpy( P->temp.f_files[P->temp.f_ntemps], "/sys/class/thermal/thermal_zone1/temp" );
    strcpy( P->temp.f_labels[P->temp.f_ntemps], "GPU" );
    P->temp.f_t_max[P->temp.f_ntemps] = 70;
    P->temp.f_t_min[P->temp.f_ntemps++] = 40;
    #endif // AWL_GPU_THERMAL_ZONE
    start_temp_thread( &P->temp, 1 );

    P->cycle_layout = cycle_layout;
    P->cycle_tag = cycle_tag;
    P->movestack = movestack;
    P->client_hide = client_hide;
    P->tagmon_f = tagmon_f;
    P->bordertoggle = bordertoggle;

    // it is imortant to set the plugin data before actually going into the bar
    // setup; the bar needs plugin data...
    S.P = P;

    int s = pthread_create( &S.BarThread, NULL, awl_bar_run, NULL );
    if (s != 0)
        awl_err_printf( "pthread create: %s", strerror(s) );
    pthread_create( &S.BarRefreshThread, NULL, awl_bar_refresh, &P->refresh_sec );

    // wallpaper somewhat separate. this is ok. it checks for awl_is_ready in
    // its own non-blocking thread
    char wpname[1024];
    strcpy( wpname, getenv("HOME") );
    strcat( wpname, "/Wallpapers/*.png" );
    wallpaper_init( wpname, 1800 );
}

static void awl_plugin_free(void) {
    awl_log_printf( "free plugin resources" );
    free(S.rules);
    free(S.layouts);
    free(S.monrules);
    free(S.keys);
    free(S.buttons);

    stop_stats_thread();
    S.P->date = NULL;
    stop_date_thread();
    S.P->pulse = NULL;
    stop_pulse_thread();
    stop_ip_thread();
    stop_bat_thread();
    stop_temp_thread();

    // again wallpaper somewhat separate
    wallpaper_destroy();

    // cancel bar refreshing
    pthread_cancel( S.BarRefreshThread );
    awl_log_printf( "cancel bar refresh thread" );

    // and cancel the bar thread itself
    // this is kinda clumsy, now that we have the minimal window that's all
    // self-contained. The bar could also be a minimal window. It's not doing
    // anything else, honestly.
    awl_state_t* B = AWL_VTABLE_SYM.state;
    awl_log_printf( "cleanup vtable state: %p", B );
    if (B) {
        awlb_run_display = false;
        int s = pthread_cancel( S.BarThread );
        awl_log_printf( "cancelled bar thread" );
        if (s != 0)
            awl_err_printf( "pthread cancel: %s", strerror(s) );
        awl_log_printf( "joining thread %p", S.BarThread );
        s = pthread_join( S.BarThread, NULL );
        awl_log_printf( "joined bar thread" );
        if (s != 0)
            awl_err_printf( "pthread join: %s", strerror(s) );
    }

    free(S.P);
    memset(&S, 0, sizeof(awl_config_t));
    awl_log_printf( "successfully freed plugin resources" );
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

static void movestack( const Arg *arg ) {
    (void)arg;
    awl_state_t* B = AWL_VTABLE_SYM.state;
    awl_config_t* C = &S;
    if (!B || !C) return;

    Client *c, *sel = focustop(B->selmon);

    if (!sel) {
        return;
    }

    if (wl_list_length(&B->clients) <= 1) {
        return;
    }

    if (arg->i > 0) {
        wl_list_for_each(c, &sel->link, link) {
            if (&c->link == &B->clients) {
                c = wl_container_of(&B->clients, c, link);
                break; /* wrap past the sentinel node */
            }
            if (VISIBLEON(c, B->selmon) || &c->link == &B->clients) {
                break; /* found it */
            }
        }
    } else {
        wl_list_for_each_reverse(c, &sel->link, link) {
            if (&c->link == &B->clients) {
                c = wl_container_of(&B->clients, c, link);
                break; /* wrap past the sentinel node */
            }
            if (VISIBLEON(c, B->selmon) || &c->link == &B->clients) {
                break; /* found it */
            }
        }
        /* backup one client */
        c = wl_container_of(c->link.prev, c, link);
    }

    wl_list_remove(&sel->link);
    wl_list_insert(&c->link, &sel->link);
    arrange(B->selmon);
    printstatus();
}

static void client_hide( const Arg* arg ) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    awl_config_t* C = &S;
    if (!B || !C) return;

    int hide = arg->ui;
    if (hide) {
        Client* sel = focustop(B->selmon);
        if (sel) sel->visible = 0;
    } else {
        Client* c;
        wl_list_for_each(c, &B->clients, link) {
            if (c && !c->visible && VISIBLEON(c, B->selmon)) {
                c->visible = 1;
                break;
            }
        }
    }
    arrange(B->selmon);
    focusclient(focustop(B->selmon), 0);
    printstatus();
}

static void tagmon_f( const Arg* arg ) {
    tagmon( arg );
    focusmon( arg );
}

static void bordertoggle( const Arg* arg ) {
    (void)arg;
    awl_state_t* B = AWL_VTABLE_SYM.state;
    awl_config_t* C = &S;
    if (!B || !C) return;
    Monitor* selmon = B->selmon;
    if (!selmon) return;
    Client* c = focustop(B->selmon);
    if (!c) return;
    c->bw = c->bw ? 0 : C->borderpx;
    arrange(selmon);
}

static void gaplessgrid(Monitor *m) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    awl_config_t* C = &S;
    if (!B || !C) return;

    unsigned int n = 0, i = 0, ch, cw, cn, rn, rows, cols;
    Client *c;

    wl_list_for_each(c, &B->clients, link)
        if (c->visible && VISIBLEON(c, m) && !c->isfloating)
            n++;
    if (n == 0)
        return;

    /* grid dimensions */
    for (cols = 0; cols <= (n / 2); cols++)
        if ((cols * cols) >= n)
            break;

    if (n == 5) /* set layout against the general calculation: not 1:2:2, but 2:3 */
        cols = 2;
    rows = n / cols;

    /* window geometries */
    cw = cols ? m->w.width / cols : (unsigned int)m->w.width;
    cn = 0; /* current column number */
    rn = 0; /* current row number */
    wl_list_for_each(c, &B->clients, link) {
        unsigned int cx, cy;
        if (!c->visible || !VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
            continue;

        if ((i / rows + 1) > (cols - n % cols))
            rows = n / cols + 1;
        ch = rows ? m->w.height / rows : (unsigned int)m->w.height;
        cx = m->w.x + cn * cw;
        cy = m->w.y + rn * ch;
        resize(c, (struct wlr_box) { .x =  cx, .y = cy, .width = cw, .height = ch }, 0);
        rn++;
        if (rn >= rows) {
            rn = 0;
            cn++;
        }
        i++;
    }
}

static void bstack(Monitor *m) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    awl_config_t* C = &S;
    if (!B || !C) return;

    int w, h, mh, mx, tx, ty, tw;
    unsigned int i, n = 0;
    Client *c;

    wl_list_for_each(c, &B->clients, link)
        if (c->visible && VISIBLEON(c, m) && !c->isfloating)
            n++;
    if (n == 0)
        return;

    if ((int)n > m->nmaster) {
        mh = m->nmaster ? m->mfact * m->w.height : 0;
        tw = m->w.width / (n - m->nmaster);
        ty = m->w.y + mh;
    } else {
        mh = m->w.height;
        tw = m->w.width;
        ty = m->w.y;
    }

    i = mx = 0;
    tx = m-> w.x;
    wl_list_for_each(c, &B->clients, link) {
        if (!c->visible || !VISIBLEON(c, m) || c->isfloating)
            continue;
        if ((int)i < m->nmaster) {
            #define MIN(A,B) ((A)<(B)?(A):(B))
            w = (m->w.width - mx) / (MIN((int)n, m->nmaster) - i);
            resize(c, (struct wlr_box) { .x = m->w.x + mx, .y = m->w.y, .width = w, .height = mh }, 0);
            mx += c->geom.width;
        } else {
            h = m->w.height - mh;
            resize(c, (struct wlr_box) { .x = tx, .y = ty, .width = tw, .height = h }, 0);
            if (tw != m->w.width)
                tx += c->geom.width;
        }
        i++;
    }
}

static void fibonacci(Monitor *mon, int s) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    awl_config_t* C = &S;
    if (!B || !C) return;

    unsigned int i=0, n=0, nx, ny, nw, nh;
    Client *c;

    wl_list_for_each(c, &B->clients, link)
        if (c->visible && VISIBLEON(c, mon) && !c->isfloating)
            n++;
    if(n == 0)
        return;

    nx = mon->w.x;
    ny = 0;
    nw = mon->w.width;
    nh = mon->w.height;

    wl_list_for_each(c, &B->clients, link)
        if (c->visible && VISIBLEON(c, mon) && !c->isfloating){
        if((i % 2 && nh / 2 > 2 * c->bw)
           || (!(i % 2) && nw / 2 > 2 * c->bw)) {
            if(i < n - 1) {
                if(i % 2)
                    nh /= 2;
                else
                    nw /= 2;
                if((i % 4) == 2 && !s)
                    nx += nw;
                else if((i % 4) == 3 && !s)
                    ny += nh;
            }
            if((i % 4) == 0) {
                if(s)
                    ny += nh;
                else
                    ny -= nh;
            }
            else if((i % 4) == 1)
                nx += nw;
            else if((i % 4) == 2)
                ny += nh;
            else if((i % 4) == 3) {
                if(s)
                    nx += nw;
                else
                    nx -= nw;
            }
            if(i == 0)
            {
                if(n != 1)
                    nw = mon->w.width * mon->mfact;
                ny = mon->w.y;
            }
            else if(i == 1)
                nw = mon->w.width - nw;
            i++;
        }
        resize(c, (struct wlr_box){.x = nx, .y = ny,
            .width = nw, .height = nh}, 0);
    }
}

static void dwindle(Monitor *mon) {
    fibonacci(mon, 1);
}

awl_vtable_t AWL_VTABLE_SYM = {
    .init = &awl_plugin_init,
    .free = &awl_plugin_free,
    .config = &S,
    .state = NULL,
};
