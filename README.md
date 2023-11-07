# awl
## fork of dwl with hot-reloading of core functionality

# features
* integrated bar (fork of dwlb) started as a thread
* CPU/RAM/SWAP widget
* clock widget
* pulseaudio widget
* temperature widget
* ip address widget
* monokai colorscheme
* some of the dwl patches included (layouts etc)

# future ideas
* lockscreen (looking at xtr-lock-pam/awesomewm)
* wallpaper support
* widgets with mouse support

# useful programs
* dunst (notifications)
* fuzzel (launcher)
* xdg-desktop-portal-wlr + wireplumber + wofi
* wdisplays + wlr-randr (need to write some scripts)
* kitty (terminal)

# compilation options
* Makefile.inc: CFLAGS (and LDFLAGS)
* -DAWL_MODKEY=WLR_MODIFIER_LOGO
* -DAWL_MENU_CMD=\"fuzzel\"
* -DAWL_TERM_CMD=\"kitty\"
* -DAWL_STATS_SKIP_CPU_MULT
