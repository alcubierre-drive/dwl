# awl

awl is a Wayland compositor built on [wlroots] and [scenefx]. It started as a
fork of [dwl], the dwm-like compositor, and has grown into a small desktop of
its own:

- a built-in bar with tags, a task bar, the layout, and status widgets (CPU,
  memory and swap graphs, temperature, battery, volume, IP address, clock)
- a system tray (StatusNotifierItem) with a calendar popup that shows
  evolution-data-server events
- a wallpaper with timed changes and fades
- a panel listing the files in `~/Desktop`
- blur and rounded corners for layer surfaces (scenefx)
- desktop notifications for its own events

Most of that lives in `libawlplugins.so`, which a running awl rebuilds and
reloads without a restart.

## Building

Dependencies:

- awl: wlroots 0.20, scenefx 0.5, wayland, xkbcommon, libinput, pixman,
  fcft, libpulse, libsystemd, and xcb and xcb-icccm for XWayland
- the tray (`tray/`): gtkmm 3, gtk-layer-shell, dbusmenu-gtk3, jsoncpp,
  spdlog, and optionally libecal for calendar events

scenefx is looked up under `~/Desktop/awl-libs` (`AWL_LIBS` in config.mk).
Then:

    make

This builds `awl`, `tray/libawltray.so` and `libawlplugins.so`. On the first
build, config.h is copied from config.def.h.

## Configuration

config.h has two halves:

- the first half is read once at startup, so a change there needs a restart
- the second half is built into `libawlplugins.so` too, so `make` followed by
  `plugin_restart` (MOD+Ctrl+r) applies it to the running awl

The second half covers keys, buttons, rules, layouts, colors, font, borders,
blur, input devices, the keymap, and the tray and wallpaper settings.

## Running

    awl [-s startup command] [-d] [-v]

`-s` runs a shell command at startup. awl sends it SIGTERM when it exits.
`-d` turns on debug logging. awl logs to `/tmp/awl.log`.

Like dwl, awl runs on any wlroots backend: nested in an X11 or Wayland
session, or directly on a VT. On a VT you need a seat, either from
logind with polkit, or from seatd. To start it from a display
manager, copy `awl.desktop` to `/usr/share/wayland-sessions/`.

`test/live.sh` runs a headless instance for testing, see the comment at its
top.

## Documentation

The documentation covers the architecture, live reload and the threads, plus
an API reference generated from the sources:

    make -C doc venv    # once
    make -C doc

Then open `doc/_build/html/index.html`.

## License

GPLv3 or later, see LICENSE. That file also carries the licenses of the code
awl inherited from dwl, dwm, sway and drwl.

## Acknowledgements

awl would not exist without [dwl] and its contributors, and through dwl
without [dwm], [TinyWL] and [sway]. The bar's drawing code started out as
[drwl].

[wlroots]: https://gitlab.freedesktop.org/wlroots/wlroots/
[scenefx]: https://github.com/wlrfx/scenefx
[dwl]: https://codeberg.org/dwl/dwl
[dwm]: https://dwm.suckless.org/
[TinyWL]: https://gitlab.freedesktop.org/wlroots/wlroots/-/tree/master/tinywl
[sway]: https://github.com/swaywm/sway
[drwl]: https://codeberg.org/sewn/drwl
