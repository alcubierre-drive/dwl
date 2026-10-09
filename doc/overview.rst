Architecture
============

awl is three binaries:

``awl``
   The compositor: wlroots (with scenefx for blur and rounded corners), the
   windows, the input, the layouts, the bar's buffers and its layout, the
   wallpaper's and the desktop panel's scene nodes. Its main thread runs the
   Wayland event loop and is the only one that touches wlroots.

``tray/libawltray.so``
   The system tray and the calendar popup (C++, gtkmm). Linked into awl and
   running on its own GTK thread; it draws into real layer-shell windows of
   its own, placed over the bar's systray slot. See :doc:`api/tray`.

``libawlplugins.so``
   Everything that is meant to change often: the plugin threads that collect
   what the bar shows, the bar widgets, the desktop panel's contents, picking
   and decoding the wallpaper, and config_plugins.h. awl loads
   it at startup and swaps in a rebuilt one on ``plugin_restart``. See
   :doc:`reload` and :doc:`api/plugin-abi`.

What lives where
----------------

.. list-table::
   :header-rows: 1
   :widths: 20 40 40

   * - Part
     - awl (main thread)
     - libawlplugins.so
   * - Bar
     - buffers, layout, pointer events, hover timers (awl.c, awl_draw.c)
     - the widgets: what they show and what clicks do (widgets.c)
   * - Status data
     - polls the redraw eventfd (plugins/redraw.c)
     - the plugins, on the poller thread (plugins/)
   * - Wallpaper
     - shows it, scaled to cover, and fades (background.c); the timer and
       the ``wallpaper``/``wallpapermode`` actions (awl.c)
     - lists the directory, picks and decodes (plugins/wallpaper.c, Wuffs)
   * - Desktop panel
     - scene nodes, buffers, placement (desktop.c)
     - scans ``desktop_dir``, lays out and draws the list, handles
       clicks on the bare desktop (desktop_panel.c)
   * - Config
     - config_awl.h, and config_plugins.def.h as a fallback
     - config_plugins.h (awl_config.c)
   * - Notifications
     -
     - spawns ``notify-send`` (plugins.c)

The library never calls into awl directly: awl hands it a table of
functions, `awl_host_t`, and gets one back, `awl_plugin_api_t`. It does read
awl's structs (`Monitor`, `Client`, `awl_draw_t`, ...), so both have to be built
from the same headers; the library checks that when it is loaded.

A redraw, end to end
--------------------

#. A plugin thread notices a change, e.g. the clock ticks, stores the new
   value in an atomic and calls `awl_redraw_request()`.
#. That writes awl's redraw eventfd. Requests coalesce until the main loop
   gets to it, so a burst costs one redraw.
#. The main loop wakes up and redraws the bars: for each right-hand widget
   it calls `widget_t.measure` to learn its width, then `widget_t.draw` for
   every widget, into a buffer it reuses.
#. The same wakeup updates the desktop panels (`desktop_update_all()`), which
   redraw only if `awl_plugin_api_t.desktop_version` changed, and takes a
   newly decoded wallpaper, if any (`awl_plugin_api_t.wallpaper_take`).

Where to start reading
----------------------

* awl_plugin_abi.h: the contract between awl and the library, and the list
  of what the library does.
* plugins/thread.h and plugins/redraw.h: how every background thread starts,
  stops and reports.
* awl_draw.h: `widget_t`, the unit the bar is made of.
* config_awl.def.h and config_plugins.def.h: the configuration, read at
  startup and reloadable.
