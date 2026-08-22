#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the tray's background thread: a GTK/GLib main loop hosting the
 * D-Bus StatusNotifierHost/Watcher and a real gtk-layer-shell overlay
 * window that renders the tray icons and owns their input directly (so
 * context-menu popups work like any normal GTK app). Safe to call once
 * during compositor startup. */
void awl_tray_init(void);

/* Requests the background thread to quit. Does NOT block until it has
 * exited -- see awl_tray_join(). (It can't safely block itself: the tray
 * thread's own Wayland client teardown may need a response from us as the
 * server, which requires the caller to keep dispatching the display's
 * event loop while waiting, not just sit in a blind pthread_join().) */
void awl_tray_shutdown(void);

/* Non-blocking poll for whether the thread awl_tray_shutdown() asked to
 * quit has actually finished (including tearing down its own window/
 * Wayland client state). Returns 1 once done (or if the tray was never
 * started), 0 otherwise. Call this in a loop that also keeps dispatching
 * dwl's own Wayland event loop (e.g. wl_event_loop_dispatch()) between
 * polls -- after wl_display_terminate(), nothing else does that any more,
 * and the tray thread may be blocked waiting for exactly that. */
int awl_tray_join(void);

/* Current width of the tray overlay window's content, in the same
 * buffer-scaled pixel space as the bar's own pixman buffer -- mirrors
 * widget_t.draw()'s return-value contract, letting dwl reserve the right
 * amount of space in the bar layout even though the tray renders itself. */
uint32_t awl_tray_width(void);

/* Tells the tray where dwl is about to place the systray widget in the
 * bar this frame, in buffer-scaled pixels relative to the bar's own left
 * edge. Combined with awl_tray_set_bar_geometry()'s monitor-space origin,
 * this auto-positions the real overlay window over that slot every frame
 * instead of relying on hardcoded margins. Call from systray_draw(). */
void awl_tray_set_widget_x(uint32_t x);

/* Keeps the tray overlay window positioned/sized to track the real bar.
 * `x`/`y`/`width`/`height` are the bar's rect in the same layout-relative
 * coordinates dwl positions the bar's own scene buffer in. */
void awl_tray_set_bar_geometry(int32_t x, int32_t y, uint32_t width, uint32_t height, double scale);

/* Shows or hides the tray overlay window. The tray is a real, separate
 * layer-shell surface, not something drawn into the bar's own buffer, so
 * hiding the bar (m->showbar) doesn't hide it on its own -- call this from
 * wherever dwl toggles bar visibility for the monitor the tray is
 * currently tracking. */
void awl_tray_set_visible(int visible);

#ifdef __cplusplus
}
#endif
