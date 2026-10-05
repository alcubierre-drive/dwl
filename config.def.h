/* Taken from https://github.com/djpohly/dwl/issues/466 */
#define COLOR(hex)    { ((hex >> 24) & 0xFF) / 255.0f, \
                        ((hex >> 16) & 0xFF) / 255.0f, \
                        ((hex >> 8) & 0xFF) / 255.0f, \
                        (hex & 0xFF) / 255.0f }
/* If you want to use the windows key for MODKEY, use WLR_MODIFIER_LOGO */
#define MODKEY WLR_MODIFIER_ALT

/* config.h has two halves. The first is read by dwl once, at startup:
 * restart dwl to apply changes there. The second is reloadable: it is built
 * into libawlplugins.so as well, so `make` and plugin_restart (MOD+Ctrl+r)
 * apply it to the running dwl -- keymap, input devices, font, colors,
 * borders, blur and layouts included (monitor rules are applied to outputs
 * appearing afterwards). dwl is built with the second half too and uses it
 * while no library is loaded. */

#ifndef AWL_CONFIG_RELOADABLE_ONLY
/* ==================== startup: restart dwl to apply ==================== */

/* appearance */
static const int showbar                   = 1; /* 0 means no bar */
static const int topbar                    = 1; /* 0 means bottom bar */
static const bool locked_blur              = true; /* blur the screen while locked */

/* tagging - TAGCOUNT must be no greater than 31 */
#define NTAGS 9
static char *tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

/* logging */
static int log_level = WLR_ERROR;

/* started with dwl, and terminated when it exits */
static const char* Autostarts[][8] = {
    { NULL }, /* e.g. { "nm-applet", NULL }, */
};
static const int ScreenLockServiceAtStart = 0; /* spawn ScreenLockService */
static const int SwwwAtStart = 0;              /* awww-daemon as the startup command (-s) */
static const char* ScreenLockService[] = { "systemd-lock-handler", "--", "swaylock", "-c", "00000000", NULL };

#endif /* AWL_CONFIG_RELOADABLE_ONLY */

/* ==================== reloadable: make && MOD+Ctrl+r ==================== */

/* appearance */
static const int sloppyfocus               = 1;  /* focus follows mouse */
static const int bypass_surface_visibility = 0;  /* 1 means idle inhibitors will disable idle tracking even if it's surface isn't visible  */
static const int borderpx                  = 1;  /* border pixel of windows */
static const char font[]                   = "monospace:size="; /* the size is appended */
static const int fontsize                  = 10;
static const float rootcolor[]             = COLOR(0x000000ff);
/* strength and alpha of every blur: windows (rules), notifications, the
 * launcher, the desktop file list and the lock screen */
static const float locked_blur_config[]    = {1.0 /*strength*/, 1.0 /*alpha*/};
/* This conforms to the xdg-protocol. Set the alpha to zero to restore the old behavior */
static const float fullscreen_bg[]         = {0.1f, 0.1f, 0.1f, 1.0f}; /* You can also use glsl colors */
/* blur behind layer surfaces with these namespaces; the launcher's also
 * applies to the desktop file list */
static const int blur_notifications        = 1, /* "notifications" */
                 blur_notifications_radius = 15,
                 blur_launcher             = 1, /* "launcher" */
                 blur_launcher_radius      = 15;

/* molokai_* are in plugins/colors.h */
static uint32_t colors[][3]                = {
	/*               fg          bg          border    */
	[SchemeNorm] = { 0xbbbbbbff, 0x222222ff, 0x444444ff },
	[SchemeSel]  = { 0xeeeeeeff, 0x005577ff, 0x005577ff },
	[SchemeUrg]  = { 0,          0,          0x770000ff },
};

/* NOTE: ALWAYS keep a rule declared even if you don't use rules (e.g leave at least one example) */
static const Rule rules[] = {
	/* app_id             title       tags mask     isfloating   monitor w h blur 1-alpha*/
	/* examples: */
	{ "Gimp_EXAMPLE",     NULL,       0,            1,           -1,   0,  0, 0, 0 }, /* Start on currently visible tags floating, not tiled */
	{ "firefox_EXAMPLE",  NULL,       1 << 8,       0,           -1,   0,  0, 0, 0 }, /* Start on ONLY tag "9" */
	{ "foot_EXAMPLE",     NULL,       0,            1,           -1, 400, 300, 1, 0.2 }, /* floating 400x300, blurred, 80% opaque */
};

/* layout(s); setlayout/cycle_layout switch between them. After a reload
 * every monitor keeps the layout at the same index. */
static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[]=",      tile },
	{ "><>",      NULL },    /* no layout function means floating behavior */
	{ "[M]",      monocle },
	{ "###",      gaplessgrid },
	{ "TTT",      bstack },
};

/* monitors */
/* (x=-1, y=-1) is reserved as an "autoconfigure" monitor position indicator
 * WARNING: negative values other than (-1, -1) cause problems with Xwayland clients
 * https://gitlab.freedesktop.org/xorg/xserver/-/issues/899
*/
/* NOTE: ALWAYS add a fallback rule, even if you are completely sure it won't be used */
static const MonitorRule monrules[] = {
	/* name       mfact  nmaster scale layout       rotate/reflect                x    y */
	/* example of a HiDPI laptop monitor:
	{ "eDP-1",    0.5f,  1,      2,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 },
	*/
	/* defaults */
	{ NULL,       0.55f, 1,      1,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 },
};

/* keyboard */
static const struct xkb_rule_names xkb_rules = {
	/* can specify fields: rules, model, layout, variant, options */
	/* example:
	.options = "ctrl:nocaps",
	*/
	.options = NULL,
};

static const int repeat_rate = 25;
static const int repeat_delay = 600;

/* Trackpad */
static const int tap_to_click = 1;
static const int tap_and_drag = 1;
static const int drag_lock = 1;
static const int natural_scrolling = 0;
static const int disable_while_typing = 1;
static const int left_handed = 0;
static const int middle_button_emulation = 0;
/* You can choose between:
LIBINPUT_CONFIG_SCROLL_NO_SCROLL
LIBINPUT_CONFIG_SCROLL_2FG
LIBINPUT_CONFIG_SCROLL_EDGE
LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN
*/
static const enum libinput_config_scroll_method scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;

/* You can choose between:
LIBINPUT_CONFIG_CLICK_METHOD_NONE
LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS
LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER
*/
static const enum libinput_config_click_method click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;

/* You can choose between:
LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
LIBINPUT_CONFIG_SEND_EVENTS_DISABLED
LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE
*/
static const uint32_t send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;

/* You can choose between:
LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT
LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE
*/
static const enum libinput_config_accel_profile accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
static const double accel_speed = 0.0;

/* You can choose between:
LIBINPUT_CONFIG_TAP_MAP_LRM -- 1/2/3 finger tap maps to left/right/middle
LIBINPUT_CONFIG_TAP_MAP_LMR -- 1/2/3 finger tap maps to left/middle/right
*/
static const enum libinput_config_tap_button_map button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

#define TAGKEYS(KEY,SKEY,TAG) \
	{ MODKEY,                    KEY,            view,            {.ui = 1ul << TAG} }, \
	{ MODKEY|WLR_MODIFIER_CTRL,  KEY,            toggleview,      {.ui = 1ul << TAG} }, \
	{ MODKEY|WLR_MODIFIER_SHIFT, SKEY,           tag,             {.ui = 1ul << TAG} }, \
	{ MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_SHIFT,SKEY,toggletag, {.ui = 1ul << TAG} }

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* commands */
static const char *termcmd[] = { "foot", NULL };
static const char *menucmd[] = { "wmenu-run", NULL };
/* MOD+w; a right click on the bare desktop runs it too */
static const char *wallpaper_cmd[] = { "random_wallpaper.sh", "-r", NULL };

/* the functions keys and buttons can call are listed in AWL_ACTIONS
 * (awl_plugin_abi.h); config.h may define its own on top, like this one */
static void tagmonf( const Arg* arg ) { tagmon(arg); focusmon(arg); }

static const Key keys[] = {
	/* Note that Shift changes certain key codes: c -> C, 2 -> at, etc. */
	/* modifier                  key                 function          argument */
	{ MODKEY,                    XKB_KEY_p,          spawn,            {.v = menucmd} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Return,     spawn,            {.v = termcmd} },
	{ MODKEY,                    XKB_KEY_w,          spawn,            {.v = wallpaper_cmd} },
	{ MODKEY,                    XKB_KEY_b,          togglebar,        {0} },
	{ MODKEY,                    XKB_KEY_j,          focusstack,       {.i = +1} },
	{ MODKEY,                    XKB_KEY_k,          focusstack,       {.i = -1} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_J,          movestack,        {.i = +1} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_K,          movestack,        {.i = -1} },
	{ MODKEY,                    XKB_KEY_i,          incnmaster,       {.i = +1} },
	{ MODKEY,                    XKB_KEY_d,          incnmaster,       {.i = -1} },
	{ MODKEY,                    XKB_KEY_h,          setmfact,         {.f = -0.05f} },
	{ MODKEY,                    XKB_KEY_l,          setmfact,         {.f = +0.05f} },
	{ MODKEY,                    XKB_KEY_Tab,        view,             {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_C,          killclient,       {0} },
	{ MODKEY,                    XKB_KEY_space,      cycle_layout,     {.i = +1} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_space,      cycle_layout,     {.i = -1} },
	{ MODKEY,                    XKB_KEY_f,          setlayout,        {.v = &layouts[1]} },
	{ MODKEY,                    XKB_KEY_g,          setlayout,        {0} }, /* the previous one */
	{ MODKEY,                    XKB_KEY_Right,      cycle_view,       {.i = +1} },
	{ MODKEY,                    XKB_KEY_Left,       cycle_view,       {.i = -1} },
	{ MODKEY|WLR_MODIFIER_CTRL,  XKB_KEY_space,      togglefloating,   {0} },
	{ MODKEY,                    XKB_KEY_e,          togglefullscreen, {0} },
	{ MODKEY,                    XKB_KEY_m,          maximize,         {0} },
	{ MODKEY,                    XKB_KEY_t,          toggleontop,      {0} },
	{ MODKEY,                    XKB_KEY_n,          minimize,         {0} },
	{ MODKEY|WLR_MODIFIER_CTRL,  XKB_KEY_n,          unminimize,       {0} },
	{ MODKEY|WLR_MODIFIER_CTRL,  XKB_KEY_r,          plugin_restart,   {0} },
	{ MODKEY,                    XKB_KEY_0,          view,             {.ui = ~0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_parenright, tag,              {.ui = ~0} },
	{ MODKEY,                    XKB_KEY_comma,      focusmon,         {.i = -1} },
	{ MODKEY,                    XKB_KEY_period,     focusmon,         {.i = +1} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_less,       tagmonf,          {.i = -1} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_greater,    tagmonf,          {.i = +1} },
	TAGKEYS(          XKB_KEY_1, XKB_KEY_exclam,                     0),
	TAGKEYS(          XKB_KEY_2, XKB_KEY_at,                         1),
	TAGKEYS(          XKB_KEY_3, XKB_KEY_numbersign,                 2),
	TAGKEYS(          XKB_KEY_4, XKB_KEY_dollar,                     3),
	TAGKEYS(          XKB_KEY_5, XKB_KEY_percent,                    4),
	TAGKEYS(          XKB_KEY_6, XKB_KEY_asciicircum,                5),
	TAGKEYS(          XKB_KEY_7, XKB_KEY_ampersand,                  6),
	TAGKEYS(          XKB_KEY_8, XKB_KEY_asterisk,                   7),
	TAGKEYS(          XKB_KEY_9, XKB_KEY_parenleft,                  8),
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Q,          quit,             {0} },

	/* Ctrl-Alt-Backspace and Ctrl-Alt-Fx used to be handled by X server */
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_Terminate_Server, quit, {0} },
	/* Ctrl-Alt-Fx is used to switch to another VT, if you don't know what a VT is
	 * do not remove them.
	 */
#define CHVT(n) { WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_XF86Switch_VT_##n, chvt, {.ui = (n)} }
	CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
	CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};

/* Clicks on the bar go to its widgets, clicks on the bare desktop to the
 * desktop file list (left: show/hide, middle: hidden files, right: MOD+w);
 * bindings here take precedence. */
static const Button buttons[] = {
	{ ClkClient,   MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove} },
	{ ClkClient,   MODKEY, BTN_MIDDLE, togglefloating, {0} },
	{ ClkClient,   MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize} },
};
