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

# useful programs
* dunst (notifications)
* fuzzel (launcher)
* xdg-desktop-portal-wlr + wireplumber + wofi
* wdisplays + wlr-randr (need to write some scripts (done?))
* kitty (terminal)
* grim (screenshots)

# compilation options
* Makefile.inc: CFLAGS (and LDFLAGS)
* -DAWL_MODKEY=WLR_MODIFIER_LOGO
* -DAWL_MENU_CMD=\"fuzzel\"
* -DAWL_TERM_CMD=\"kitty\"
* -DAWL_STATS_FORCE_CPU_MULT
* -DAWL_SKIP_BATWIDGET

# dbus interface
awl is built with some very rudimentary dbus support. Using ``dbus-send``, one
can talk to the interface ``INTERFACE`` with argument ``ARG`` as follows:
```bash
dbus-send org.freedesktop.awl / org.freedesktop.awl.INTERFACE string:ARG
```

awl supports the following interfaces/arguments:

| interface     | argument     | effect                               |
| ------------- | ------------ | ------------------------------------ |
| ``wallpaper`` | ``left``     | left click on the wallpaper          |
| ``wallpaper`` | ``right``    | right click on the wallpaper         |
| ``wallpaper`` | ``middle``   | middle click on the wallpaper        |
| ``bar``       | ``show``     | show all bars                        |
| ``bar``       | ``hide``     | hide all bars                        |
| ``bar``       | ``toggle``   | toggle all bars                      |
