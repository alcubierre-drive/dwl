#define _GNU_SOURCE
#include <sys/mman.h>

#include "init.h"
#include "../awl_log.h"
#include "../awl_client.h"
#include "bar.h"
#include "date.h"
#include "stats.h"
#include "pulsetest.h"
#include "temp.h"
#include "wallpaper.h"
#include "vector.h"
#include <assert.h>

#include "../awl_pthread.h"

#define MIN(A,B) ((A)<(B)?(A):(B))
#define MAX(A,B) ((A)>(B)?(A):(B))
#define SHOULD_BE_TILED( c ) (!(c)->isfullscreen && !(c)->isfloating && !(c)->maximized)
#define SHOULD_BE_MAXED( c ) (!(c)->isfullscreen && (c)->maximized)

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

#define ARRAY_INIT( type, ary, capacity ) S. ary = (type*)calloc( capacity, sizeof(type) );
#define ARRAY_APPEND( type, ary, ... ) S. ary[S.n_##ary ++] = (type){__VA_ARGS__};

static void movestack( const Arg *arg );
static void client_hide( const Arg* arg );
static void client_max( const Arg* arg );
static void tagmon_f( const Arg* arg );
static void bordertoggle( const Arg* arg );
static void cycle_tag( const Arg* arg );
static void cycle_layout( const Arg* arg );
static void focusstack(const Arg *arg);
static void killclient(const Arg *arg);
static void incnmaster(const Arg *arg);
static void focusmon(const Arg *arg);
static void moveresize(const Arg *arg);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void togglebar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void togglefullscreen(const Arg *arg);
static void toggleontop(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void view(const Arg *arg);

static void gaplessgrid(Monitor *m);
static void bstack(Monitor *m);
static void dwindle(Monitor *mon);
static void tile(Monitor *m);
static void monocle(Monitor *m);

static void spawn_from_plugin( const Arg* arg );

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

pid_t spawn_pid_str_s( const char* cmd ) {
    const int bufsize = 1024;
    const int argc = 16;

    char *buf_ = calloc(1, bufsize);
    if (!buf_) return 0;

    char *buf = buf_;
    int buf_remain = bufsize;

    char **argv = (char**)buf;
    argv[argc-1] = buf + bufsize - 1;

    #define ADVANCE_BUF( len ) \
    buf += (len); \
    buf_remain -= (len);
    char *argstr = ADVANCE_BUF( sizeof(char**)*argc );

    strncpy( argstr, cmd, buf_remain-2 );

    int argcount = 0;
    for (char *c = argstr; *c; c++) {
        if (*c == ' ' && argcount < argc-2) {
            *c = '\0';
            argv[argcount++] = argstr;
            argstr = c+1;
        }
    }
    argv[argcount++] = argstr;
    argv[argcount++] = argv[argc-1];

    pid_t result = spawn_pid( argv );

    free(buf_);
    return result;
}

pid_t spawn_pid_str( const char* cmd ) {
    vector_t argv_vec = vector_init( NULL, sizeof(char*) );

    // split string into vector
    char *token, *str, *tofree;
    tofree = str = strdup(cmd);
    char *s = NULL;
    while ((token = strsep(&str, " "))) {
        s = strdup(token);
        vector_push_back( &argv_vec, &s );
    }
    free( tofree );
    s = NULL, vector_push_back( &argv_vec, &s );
    char** argv = vector_data( &argv_vec );

    // spawn pid
    pid_t pid = spawn_pid( argv );

    // free resources
    for (char** F = argv; *F; F++) free(*F);
    vector_destroy( &argv_vec );

    // result
    return pid;
}

pid_t spawn_pid( char** arg ) {
    int pid_fd = memfd_create("awl_spawn_pid", MFD_ALLOW_SEALING|MFD_CLOEXEC);
    if (pid_fd == -1)
        return 0;
    ftruncate( pid_fd, sizeof(pid_t) );

    pid_t pid = fork();
    if (pid == 0) {
        pid_t ppid = fork();
        if (ppid == 0) {
            close(pid_fd);
            execvp(arg[0], arg);
            P_awl_err_printf("execvp %s failed: %s", arg[0], strerror(errno));
        } else {
            lseek(pid_fd, 0, SEEK_SET);
            write(pid_fd, &ppid, sizeof(pid_t));
            close(pid_fd);
            _exit(0);
        }
    } else {
        waitpid(pid, NULL, 0);
        lseek(pid_fd, 0, SEEK_SET);
        read(pid_fd, &pid, sizeof(pid_t));
        close(pid_fd);
    }

    return pid;
}

typedef struct awl_persistent_plugin_data_t {
    union { struct {

    bool oneshot;
    bool touched;
    bool ignore;

    }; uint64_t _padding[16]; };

    pid_t pid_tray;
    pid_t pid_nextcloud;

    pid_t pid_nm_applet;
    pid_t pid_blueman;
    pid_t pid_printer;
    pid_t pid_telegram;
    pid_t pid_evolution;
    pid_t pid_locker;
} awl_persistent_plugin_data_t;

static void awl_plugin_init(void) {

    P_awl_log_printf("persistent data…");
    awl_persistent_plugin_data_t* data = AWL_VTABLE_SYM.state->persistent_plugin_data;
    if (sizeof(awl_persistent_plugin_data_t) > AWL_VTABLE_SYM.state->persistent_plugin_data_nbytes) {
        P_awl_err_printf("too little space for plugin data");
        data = NULL;
    }
    if (data) {
        if (!data->touched) {
            data->oneshot = true; // TODO this should not live here
            data->touched = true;
        }
        #define AUTOSTART( thing, cmd ) { \
            if ( (!data->ignore && data->oneshot && !data->pid_##thing) || \
                 (!data->ignore && !data->oneshot && \
                    (kill(data->pid_##thing, 0) == -1 || !data->pid_##thing)) ) { \
                P_awl_log_printf( "autostarting " #thing ); \
                data->pid_##thing = spawn_pid_str( cmd ); \
            } \
        }
        /* AUTOSTART( tray, "./tray/awl_tray" ); */
        /* AUTOSTART( nextcloud, "nextcloud --background" ); */

        AUTOSTART( nm_applet, "nm-applet" );
        AUTOSTART( blueman, "blueman-applet" );
        AUTOSTART( printer, "system-config-printer-applet" );
        AUTOSTART( telegram, "telegram-desktop" );
        AUTOSTART( evolution, "evolution" );
        AUTOSTART( locker, "systemd-lock-handler swaylock" );
    }

    P_awl_log_printf("setting up environment");
    setenv("MOZ_ENABLE_WAYLAND", "1", 1);
    setenv("QT_STYLE_OVERRIDE","kvantum",1);
    setenv("DESKTOP_SESSION","kde",1);
    setenv("QT_AUTO_SCREEN_SCALE_FACTOR","0",1);
    setenv("EDITOR","nvim",1);
    setenv("SYSTEMD_EDITOR","/usr/bin/nvim",1);
    setenv("SSH_AUTH_SOCK","1",1);
    setenv("NO_AT_BRIDGE","1",1);
    char buf[256] = {0};
    strcpy( buf, getenv("HOME") );
    strcat( buf, "/Desktop" );
    setenv("GRIM_DEFAULT_DIR", buf, 1);

    P_awl_log_printf( "general setup" );
    S.sloppyfocus = 1;
    S.bypass_surface_visibility = 0;
    S.borderpx = 2;
    S.setlayout = setlayout;

    P_awl_log_printf( "color setup" );
    COLOR_SET( S.bordercolor, molokai_light_gray );
    COLOR_SET( S.focuscolor, molokai_blue );
    COLOR_SET( S.urgentcolor, molokai_orange );
    COLOR_SET( S.fullscreen_bg, 0x00000000 );
    setup_bar_colors();

    /* id, title, tags, {isfloating, ontop}, monitor */
    ARRAY_INIT(Rule, rules, 32);
    // tag rules
    ARRAY_APPEND(Rule, rules, "evolution", NULL, 1<<8, {0, 0}, -1 );
    ARRAY_APPEND(Rule, rules, "telegram", NULL, 1<<7, {0, 0}, -1 );
    // floating rules
    ARRAY_APPEND(Rule, rules, "nomacs", NULL, 0, {1, 0}, -1 );
    ARRAY_APPEND(Rule, rules, "python3", "Figure", 0, {1, 0}, -1 );
    ARRAY_APPEND(Rule, rules, "wdisplays", NULL, 0, {1, 0}, -1 );
    ARRAY_APPEND(Rule, rules, "blueman-manager", NULL, 0, {1, 0}, -1 );
    ARRAY_APPEND(Rule, rules, "zoom", NULL, 0, {1, 0}, -1 );
    ARRAY_APPEND(Rule, rules, "evolution-alarm-notify", NULL, 1<<8, {1, 0}, -1 );
    P_awl_log_printf( "created %i rules", S.n_rules );

    ARRAY_INIT(Layout, layouts, 16);
    ARRAY_APPEND(Layout, layouts, "[◻]", gaplessgrid );
    ARRAY_APPEND(Layout, layouts, "[M]", monocle );
    ARRAY_APPEND(Layout, layouts, "[=]", bstack );
    ARRAY_APPEND(Layout, layouts, "[T]", tile );
    ARRAY_APPEND(Layout, layouts, "[@]", dwindle );
    S.cur_layout = 0;
    P_awl_log_printf( "created %i layouts", S.n_layouts );

    /* name, mfact, nmaster, scale, layout, rotate/reflect, x, y */
    ARRAY_INIT(MonitorRule, monrules, 16);
    ARRAY_APPEND(MonitorRule, monrules, NULL, 0.5, 1, 1.0, 0, WL_OUTPUT_TRANSFORM_NORMAL, -1, -1);
    P_awl_log_printf( "created %i monitor rules", S.n_monrules );

    P_awl_log_printf( "keyboard/mouse config" );
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
    #define AWL_TERM_CMD "kitty -d $HOME"
    #endif
    #ifndef AWL_MENU_CMD
    #define AWL_MENU_CMD "bemenu-run"
    #endif

    ADD_KEY( MODKEY,    XKB_KEY_p,          spawn_from_plugin,  {.v=AWL_MENU_CMD} )
    ADD_KEY( MODKEY,    XKB_KEY_Return,     spawn_from_plugin,  {.v=AWL_TERM_CMD} )
    ADD_KEY( MODKEY,    XKB_KEY_j,          focusstack,         {.i = +1} )
    ADD_KEY( MODKEY,    XKB_KEY_k,          focusstack,         {.i = -1} )
    /* ADD_KEY( MODKEY,    XKB_KEY_i,          incnmaster,         {.i = +1} ) */
    /* ADD_KEY( MODKEY,    XKB_KEY_d,          incnmaster,         {.i = -1} ) */
    ADD_KEY( MODKEY,    XKB_KEY_h,          setmfact,           {.f = -0.05} )
    ADD_KEY( MODKEY,    XKB_KEY_l,          setmfact,           {.f = +0.05} )
    ADD_KEY( MODKEY,    XKB_KEY_Tab,        view,               {0} )
    ADD_KEY( MODKEY_SH, XKB_KEY_C,          killclient,         {0} )
    ADD_KEY( MODKEY_CT, XKB_KEY_space,      togglefloating,     {0} )
    ADD_KEY( MODKEY,    XKB_KEY_f,          togglefullscreen,   {0} )
    ADD_KEY( MODKEY,    XKB_KEY_t,          toggleontop,        {0} )
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
    ADD_KEY( MODKEY,    XKB_KEY_m,          client_max,         {.i=0} )

    ADD_KEY( MODKEY,    XKB_KEY_Right,      cycle_tag,          {.i= 1} )
    ADD_KEY( MODKEY,    XKB_KEY_Left,       cycle_tag,          {.i=-1} )
    ADD_KEY( MODKEY,    XKB_KEY_space,      cycle_layout,       {.i= 1} )
    ADD_KEY( MODKEY_SH, XKB_KEY_space,      cycle_layout,       {.i=-1} )
    ADD_KEY( MODKEY_SH, XKB_KEY_J,          movestack,          {.i = +1} )
    ADD_KEY( MODKEY_SH, XKB_KEY_K,          movestack,          {.i = -1} )

    ADD_KEY( MODKEY,    XKB_KEY_b,          bordertoggle,       {0} )

    ADD_KEY( 0, XKB_KEY_XF86MonBrightnessUp,  spawn_from_plugin,{.v="backlight-tooler -m inc -V 0.05"} );
    ADD_KEY( 0, XKB_KEY_XF86MonBrightnessDown,spawn_from_plugin,{.v="backlight-tooler -m dec -V 0.05"} );
    ADD_KEY( 0, XKB_KEY_XF86AudioRaiseVolume, spawn_from_plugin,{.v="pactl set-sink-volume @DEFAULT_SINK@ +5%"} );
    ADD_KEY( 0, XKB_KEY_XF86AudioLowerVolume, spawn_from_plugin,{.v="pactl set-sink-volume @DEFAULT_SINK@ -5%"} );
    ADD_KEY( 0, XKB_KEY_XF86AudioMute,        spawn_from_plugin,{.v="pactl set-sink-mute @DEFAULT_SINK@ toggle"} );
    ADD_KEY( 0, XKB_KEY_XF86AudioMicMute,     spawn_from_plugin,{.v="pactl set-source-mute @DEFAULT_SOURCE@ toggle"} );
    ADD_KEY( 0, XKB_KEY_Print,                spawn_from_plugin,{.v="grim"} );
    ADD_KEY( MODKEY, XKB_KEY_F1,              spawn_from_plugin,{.v="pulse_port_switch -t -N"} );
    ADD_KEY( MODKEY, XKB_KEY_d,               spawn_from_plugin,{.v="wdisplays"} );
    ADD_KEY( 0, XKB_KEY_XF86Display,          spawn_from_plugin,{.v="wdisplays"} );
    ADD_KEY( MODKEY_SH, XKB_KEY_G,            spawn_from_plugin,{.v="swaylock"} );

    ADD_KEY( MODKEY_CT_SH, XKB_KEY_D,         spawn_from_plugin,{.v="docked 1"} );
    ADD_KEY( MODKEY_CT,    XKB_KEY_d,         spawn_from_plugin,{.v="docked 2"} );
    ADD_KEY( MODKEY_SH,    XKB_KEY_D,         spawn_from_plugin,{.v="docked 3"} );
    ADD_KEY( MODKEY_CT,    XKB_KEY_u,         spawn_from_plugin,{.v="docked 4"} );
    ADD_KEY( MODKEY_SH,    XKB_KEY_U,         spawn_from_plugin,{.v="docked 5"} );
    ADD_KEY( MODKEY_CT_SH, XKB_KEY_U,         spawn_from_plugin,{.v="docked 6"} );

    // TODO potentially missing keyboard shortcuts
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
    P_awl_log_printf( "created %i keybindings", S.n_keys );

    ARRAY_INIT(Button, buttons, 16);
    ARRAY_APPEND(Button, buttons, MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove});
    ARRAY_APPEND(Button, buttons, MODKEY, BTN_MIDDLE, togglefloating, {0});
    ARRAY_APPEND(Button, buttons, MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize});
    P_awl_log_printf( "created %i mousebindings", S.n_buttons );

    awl_plugin_data_t* P = calloc(1, sizeof(awl_plugin_data_t));
    P->refresh_sec = 0.2;

    P->pulse = start_pulse_thread();
    P->date = start_date_thread( 10 );
    P->ip = start_ip_thread( 1 );
    P->stats = start_stats_thread( 32, 32, 32, 1 );
    P->stats->dir = 0;
    P->bat = start_bat_thread( 1 );

    P->temp = calloc(1,sizeof(awl_temperature_t));
    #ifndef AWL_CPU_THERMAL_ZONE
    strcpy( P->temp->f_files[P->temp->f_ntemps], "/sys/class/thermal/thermal_zone7/temp" );
    #else // AWL_CPU_THERMAL_ZONE
    strcpy( P->temp->f_files[P->temp->f_ntemps], "/sys/class/thermal/" AWL_CPU_THERMAL_ZONE "/temp" );
    #endif // AWL_CPU_THERMAL_ZONE
    #ifndef AWL_CPU_THERMAL_ZONE_NAME
    strcpy( P->temp->f_labels[P->temp->f_ntemps], "CPU" );
    #else
    strcpy( P->temp->f_labels[P->temp->f_ntemps], AWL_CPU_THERMAL_ZONE_NAME );
    #endif
    P->temp->f_t_max[P->temp->f_ntemps] = 80;
    P->temp->f_t_min[P->temp->f_ntemps++] = 40;
    #ifdef AWL_GPU_THERMAL_ZONE
    strcpy( P->temp->f_files[P->temp->f_ntemps], "/sys/class/thermal/thermal_zone1/temp" );
    strcpy( P->temp->f_labels[P->temp->f_ntemps], "GPU" );
    P->temp->f_t_max[P->temp->f_ntemps] = 70;
    P->temp->f_t_min[P->temp->f_ntemps++] = 40;
    #endif // AWL_GPU_THERMAL_ZONE
    start_temp_thread( P->temp, 1 );

    P->cycle_layout = cycle_layout;
    P->cycle_tag = cycle_tag;
    P->movestack = movestack;
    P->client_hide = client_hide;
    P->client_max = client_max;
    P->tagmon_f = tagmon_f;
    P->bordertoggle = bordertoggle;
    P->focusstack = focusstack;
    P->toggleview = toggleview;
    P->view = view;

    P->cal = calendar_popup();

    // it is imortant to set the plugin data before actually going into the bar
    // setup; the bar needs plugin data...
    S.P = P;
    S.bars = awl_bar_run( P->refresh_sec );

    char wpname[1024]; strcpy( wpname, getenv("HOME") ); strcat( wpname, "/Wallpapers/*.png" );
    P->wp = wallpaper_init( wpname, 1800 );
}

static void awl_plugin_free(void) {
    P_awl_log_printf( "free plugin resources" );
    S.n_rules = 0;
    S.n_layouts = 0;
    S.n_monrules = 0;
    S.n_keys = 0;
    S.n_buttons = 0;
    free(S.rules);
    free(S.layouts);
    free(S.monrules);
    free(S.keys);
    free(S.buttons);

    P_awl_log_printf( "cancel bar thread" );
    awl_bar_stop( S.bars );

    // now to all the plugin data threads
    P_awl_log_printf("stop stats thread");
    stop_stats_thread(S.P->stats);
    P_awl_log_printf("stop date thread");
    stop_date_thread(S.P->date);
    P_awl_log_printf("stop ip_thread");
    stop_ip_thread( S.P->ip );
    P_awl_log_printf("stop bat_thread");
    stop_bat_thread( S.P->bat );
    P_awl_log_printf("stop temp_thread");
    stop_temp_thread( S.P->temp );
    free( S.P->temp );
    P_awl_log_printf("stop pulse thread");
    stop_pulse_thread(S.P->pulse);

    P_awl_log_printf("destroy cal");
    if (S.P->cal) calendar_destroy( S.P->cal );
    P_awl_log_printf("destroy wallpaper");
    if (S.P->wp) wallpaper_destroy(S.P->wp);

    // free the plugin data somewhat last
    free(S.P);
    memset(&S, 0, sizeof(awl_config_t));
    P_awl_log_printf( "successfully freed plugin resources" );
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
    B->focusclient(B->focustop(selmon), 1);
    B->arrange(selmon);
    B->printstatus();
}

static void cycle_layout( const Arg* arg ) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    if (arg->i > 0)
        S.cur_layout = (S.cur_layout+1) % S.n_layouts;
    else
        S.cur_layout = (S.cur_layout + S.n_layouts-1) % S.n_layouts;

    Arg A = {.i = S.cur_layout};
    setlayout( &A );
    B->focusclient(B->focustop(B->selmon), 1);
}

static void movestack( const Arg *arg ) {
    (void)arg;
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    Client *c, *sel = B->focustop(B->selmon);

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
    B->arrange(B->selmon);
    B->printstatus();
}

static void client_hide( const Arg* arg ) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    Client *c = NULL;
    int hide = arg->ui;
    if (hide) {
        c = B->focustop(B->selmon);
        if (c) c->visible = 0;
    } else {
        wl_list_for_each(c, &B->clients, link) {
            if (c && !c->visible && VISIBLEON(c, B->selmon)) {
                c->visible = 1;
                break;
            }
        }
    }
    B->arrange(B->selmon);
    if (c) {
        c = c->visible ? c : B->focustop(B->selmon);
        B->focusclient(c, 1);
    }
    B->printstatus();
}

static void client_max( const Arg* arg ) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    Client* sel = B->focustop(B->selmon);
    if (sel) {
        switch (arg->i) {
            case -1: sel->maximized = 0; break;
            case  1: sel->maximized = 1; break;
            case  0:
            default: sel->maximized = !sel->maximized; break;
        }
        if (sel->isfloating) {
            if (sel->maximized) sel->prev = sel->geom;
            else                sel->geom = sel->prev;
        }
        if (sel->isfloating && !sel->maximized) B->resize(sel, sel->prev, 0);
    }
    B->arrange(B->selmon);
    B->printstatus();
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
    Client* c = B->focustop(B->selmon);
    if (!c) return;
    c->bw = c->bw ? 0 : C->borderpx;
    B->arrange(selmon);
}

static void gaplessgrid(Monitor *m) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    unsigned int n = 0, i = 0, ch, cw, cn, rn, rows, cols;
    Client *c;

    wl_list_for_each(c, &B->clients, link)
        if (c->visible && VISIBLEON(c, m) && SHOULD_BE_TILED(c))
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
        if (!c->visible || !VISIBLEON(c, m))
            continue;
        if (!SHOULD_BE_TILED(c)) {
            if (SHOULD_BE_MAXED(c))
                B->resize(c, m->w, 0);
            continue;
        }
        unsigned int cx, cy;

        if ((i / rows + 1) > (cols - n % cols))
            rows = n / cols + 1;
        ch = rows ? m->w.height / rows : (unsigned int)m->w.height;
        cx = m->w.x + cn * cw;
        cy = m->w.y + rn * ch;
        B->resize(c, (struct wlr_box) { .x =  cx, .y = cy, .width = cw, .height = ch }, 0);
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
    if (!B) return;

    int w, h, mh, mx, tx, ty, tw;
    unsigned int i, n = 0;
    Client *c;

    wl_list_for_each(c, &B->clients, link)
        if (c->visible && VISIBLEON(c, m) && SHOULD_BE_TILED(c))
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
        if (!c->visible || !VISIBLEON(c, m))
            continue;
        if (!SHOULD_BE_TILED(c)) {
            if (SHOULD_BE_MAXED(c))
                B->resize(c, m->w, 0);
            continue;
        }
        if ((int)i < m->nmaster) {
            w = (m->w.width - mx) / (MIN((int)n, m->nmaster) - i);
            B->resize(c, (struct wlr_box) { .x = m->w.x + mx, .y = m->w.y, .width = w, .height = mh }, 0);
            mx += c->geom.width;
        } else {
            h = m->w.height - mh;
            B->resize(c, (struct wlr_box) { .x = tx, .y = ty, .width = tw, .height = h }, 0);
            if (tw != m->w.width)
                tx += c->geom.width;
        }
        i++;
    }
}

static void fibonacci(Monitor *mon, int s) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    unsigned int i=0, n=0, nx, ny, nw, nh;
    Client *c;

    wl_list_for_each(c, &B->clients, link)
        if (c->visible && VISIBLEON(c, mon) && SHOULD_BE_TILED(c))
            n++;
    if(n == 0)
        return;

    nx = mon->w.x;
    ny = 0;
    nw = mon->w.width;
    nh = mon->w.height;

    wl_list_for_each(c, &B->clients, link) {
        if (!c->visible || !VISIBLEON(c, mon))
            continue;
        if (!SHOULD_BE_TILED(c)) {
            if (SHOULD_BE_MAXED(c))
                B->resize(c, mon->w, 0);
            continue;
        }
        if ((i % 2 && nh / 2 > 2 * c->bw) || (!(i % 2) && nw / 2 > 2 * c->bw)) {
            if (i < n - 1) {
                if (i % 2)
                    nh /= 2;
                else
                    nw /= 2;
                if ((i % 4) == 2 && !s)
                    nx += nw;
                else if ((i % 4) == 3 && !s)
                    ny += nh;
            }
            if ((i % 4) == 0) {
                if(s)
                    ny += nh;
                else
                    ny -= nh;
            }
            else if ((i % 4) == 1)
                nx += nw;
            else if ((i % 4) == 2)
                ny += nh;
            else if ((i % 4) == 3) {
                if(s)
                    nx += nw;
                else
                    nx -= nw;
            }
            if (i == 0) {
                if (n != 1)
                    nw = mon->w.width * mon->mfact;
                ny = mon->w.y;
            }
            else if (i == 1)
                nw = mon->w.width - nw;
            i++;
        }
        B->resize(c, (struct wlr_box){.x = nx, .y = ny, .width = nw, .height = nh}, 0);
    }
}

static void dwindle(Monitor *mon) {
    fibonacci(mon, 1);
}

static void tile(Monitor *m) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    unsigned int i, n = 0, mw, my, ty;
    Client *c;

    wl_list_for_each(c, &B->clients, link)
        if (c->visible && VISIBLEON(c, m) && SHOULD_BE_TILED(c))
            n++;
    if (n == 0)
        return;

    if (n > (unsigned)m->nmaster)
        mw = m->nmaster ? m->w.width * m->mfact : 0;
    else
        mw = m->w.width;
    i = my = ty = 0;
    wl_list_for_each(c, &B->clients, link) {
        if (!c->visible || !VISIBLEON(c, m))
            continue;
        if (!SHOULD_BE_TILED(c)) {
            if (SHOULD_BE_MAXED(c))
                B->resize(c, m->w, 0);
            continue;
        }
        if (i < (unsigned)m->nmaster) {
            B->resize(c, (struct wlr_box){.x = m->w.x, .y = m->w.y + my, .width = mw,
                .height = (m->w.height - my) / (MIN(n, (unsigned int)m->nmaster) - i)}, 0);
            my += c->geom.height;
        } else {
            B->resize(c, (struct wlr_box){.x = m->w.x + mw, .y = m->w.y + ty,
                .width = m->w.width - mw, .height = (m->w.height - ty) / (n - i)}, 0);
            ty += c->geom.height;
        }
        i++;
    }
}

static void monocle(Monitor *m) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    Client *c;
    int n = 0;

    wl_list_for_each(c, &B->clients, link) {
        if (c->visible && VISIBLEON(c, m) && SHOULD_BE_TILED(c)) {
            B->resize(c, m->w, 0);
            n++;
        }
    }
    if (n) {
        snprintf(m->ltsymbol, sizeof(m->ltsymbol)-1, "[%d]", n);
        m->ltsymbol[sizeof(m->ltsymbol)-1] = '\0';
    }
    if ((c = B->focustop(m)))
        wlr_scene_node_raise_to_top(&c->scene->node);
}

static void spawn_from_plugin( const Arg* arg ) {
    spawn_pid_str( arg->v );
}

static void incnmaster(const Arg *arg) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    if (!arg || !B->selmon) return;
    B->selmon->nmaster = MAX(B->selmon->nmaster + arg->i, 0);
    B->arrange(B->selmon);
}

static void focusmon(const Arg *arg) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    int i = 0, nmons = wl_list_length(&B->mons);
    if (nmons)
        do /* don't switch to disabled mons */
            B->selmon = B->dirtomon(arg->i);
        while (!B->selmon->wlr_output->enabled && i++ < nmons);
    B->focusclient(B->focustop(B->selmon), 1);
}

static void focusstack(const Arg *arg) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    /* Focus the next or previous client (in tiling order) on selmon */
    Client *c, *sel = B->focustop(B->selmon);
    if (!sel || sel->isfullscreen)
        return;
    if (arg->i > 0) {
        wl_list_for_each(c, &sel->link, link) {
            if (&c->link == &B->clients)
                continue; /* wrap past the sentinel node */
            if (VISIBLEON(c, B->selmon) && c->visible)
                break; /* found it */
        }
    } else {
        wl_list_for_each_reverse(c, &sel->link, link) {
            if (&c->link == &B->clients)
                continue; /* wrap past the sentinel node */
            if (VISIBLEON(c, B->selmon) && c->visible)
                break; /* found it */
        }
    }
    /* If only one client is visible on selmon, then c == sel */
    B->focusclient(c, 1);
}

static void killclient(const Arg *arg) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    (void)arg;

    Client *sel = B->focustop(B->selmon);
    if (sel) client_send_close(sel);
}

static void moveresize(const Arg *arg) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    if (B->cursor_mode != CurNormal && B->cursor_mode != CurPressed)
        return;
    B->xytonode(B->cursor->x, B->cursor->y, NULL, &B->grabc, NULL, NULL, NULL);
    if (!B->grabc || client_is_unmanaged(B->grabc) || B->grabc->isfullscreen)
        return;

    /* Float the window and tell motionnotify to grab it */
    B->setfloating(B->grabc, 1);
    switch (B->cursor_mode = arg->ui) {
    case CurMove:
        B->grabcx = B->cursor->x - B->grabc->geom.x;
        B->grabcy = B->cursor->y - B->grabc->geom.y;
        strcpy(B->cursor_image, "fleur");
        wlr_xcursor_manager_set_cursor_image(B->cursor_mgr, B->cursor_image, B->cursor);
        break;
    case CurResize:
        /* Doesn't work for X11 output - the next absolute motion event
         * returns the cursor to where it started */
        wlr_cursor_warp_closest(B->cursor, NULL,
                B->grabc->geom.x + B->grabc->geom.width,
                B->grabc->geom.y + B->grabc->geom.height);
        strcpy(B->cursor_image,"bottom_right_corner");
        wlr_xcursor_manager_set_cursor_image(B->cursor_mgr, B->cursor_image, B->cursor);
        break;
    }
}

static void setlayout(const Arg *arg) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    awl_config_t* C = &S;
    if (!B || !C) return;

    if (!B->selmon || !arg)
        return;
    if (arg->i < C->n_layouts && arg->i >= 0) {
        B->selmon->sellt ^= 1;
        B->selmon->lt[B->selmon->sellt] = arg->i;
    }
    strncpy(B->selmon->ltsymbol, C->layouts[B->selmon->lt[B->selmon->sellt]].symbol,
            sizeof(B->selmon->ltsymbol)-1);
    B->selmon->ltsymbol[sizeof(B->selmon->ltsymbol)-1] = '\0';
    B->arrange(B->selmon);
    B->printstatus();
}

/* arg > 1.0 will set mfact absolutely */
static void setmfact(const Arg *arg) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    awl_config_t* C = &S;
    if (!B || !C) return;

    float f;

    if (!arg || !B->selmon || !C->layouts[B->selmon->lt[B->selmon->sellt]].arrange)
        return;
    f = arg->f < 1.0 ? arg->f + B->selmon->mfact : arg->f - 1.0;
    if (f < 0.1 || f > 0.9)
        return;
    B->selmon->mfact = f;
    B->arrange(B->selmon);
}

static void tag(const Arg *arg) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    Client *sel = B->focustop(B->selmon);
    if (!sel || (arg->ui & TAGMASK) == 0)
        return;

    sel->tags = arg->ui & TAGMASK;
    B->focusclient(B->focustop(B->selmon), 1);
    B->arrange(B->selmon);
    B->printstatus();
}

static void tagmon(const Arg *arg) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    Client *sel = B->focustop(B->selmon);
    if (sel)
        B->setmon(sel, B->dirtomon(arg->i), 0);
}

static void togglebar(const Arg *arg) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    (void)arg;
    DwlIpcOutput *ipc_output;
    wl_list_for_each(ipc_output, &B->selmon->dwl_ipc_outputs, link)
        B->ipc_send_toggle_vis(ipc_output->resource);
}

static void togglefloating(const Arg *arg) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    (void)arg;
    Client *sel = B->focustop(B->selmon);
    if (sel && !sel->isfullscreen) B->setfloating(sel, !sel->isfloating);
}

static void togglefullscreen(const Arg *arg) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    (void)arg;
    Client *sel = B->focustop(B->selmon);
    if (sel) B->setfullscreen(sel, !sel->isfullscreen);
}

static void toggleontop(const Arg *arg) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    (void)arg;
    Client *sel = B->focustop(B->selmon);
    if (sel && !sel->isfullscreen) B->setontop(sel, !sel->isontop);
}

static void toggletag(const Arg *arg) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    uint32_t newtags;
    Client *sel = B->focustop(B->selmon);
    if (!sel)
        return;
    newtags = sel->tags ^ (arg->ui & TAGMASK);
    if (!newtags)
        return;

    sel->tags = newtags;
    B->focusclient(B->focustop(B->selmon), 1);
    B->arrange(B->selmon);
    B->printstatus();
}

static void toggleview(const Arg *arg) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    uint32_t newtagset = B->selmon ? B->selmon->tagset[B->selmon->seltags] ^ (arg->ui & TAGMASK) : 0;

    if (!newtagset)
        return;

    B->selmon->tagset[B->selmon->seltags] = newtagset;
    B->focusclient(B->focustop(B->selmon), 1);
    B->arrange(B->selmon);
    B->printstatus();
}

static void view(const Arg *arg) {
    awl_state_t* B = AWL_VTABLE_SYM.state;
    if (!B) return;

    if (!B->selmon || (arg->ui & TAGMASK) == B->selmon->tagset[B->selmon->seltags])
        return;
    B->selmon->seltags ^= 1; /* toggle sel tagset */
    if (arg->ui & TAGMASK)
        B->selmon->tagset[B->selmon->seltags] = arg->ui & TAGMASK;
    B->focusclient(B->focustop(B->selmon), 1);
    B->arrange(B->selmon);
    B->printstatus();
}

awl_vtable_t AWL_VTABLE_SYM = {
    .init = &awl_plugin_init,
    .free = &awl_plugin_free,
    .config = &S,
    .state = NULL,
};
