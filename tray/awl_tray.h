#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the tray's background thread: a GTK/GLib main loop hosting the
 * D-Bus StatusNotifierHost/Watcher and, per monitor, a real gtk-layer-shell
 * overlay window that renders that monitor's tray icons and owns their
 * input directly (so context-menu popups work like any normal GTK app).
 * Safe to call once during compositor startup. */
void awl_tray_init(void);

/* Requests the background thread to quit. Does NOT block until it has
 * exited -- see awl_tray_join(). (It can't safely block itself: the tray
 * thread's own Wayland client teardown may need a response from us as the
 * server, which requires the caller to keep dispatching the display's
 * event loop while waiting, not just sit in a blind pthread_join().) */
void awl_tray_shutdown(void);

/* Non-blocking poll for whether the thread awl_tray_shutdown() asked to
 * quit has actually finished (including tearing down its own windows/
 * Wayland client state). Returns 1 once done (or if the tray was never
 * started), 0 otherwise. Call this in a loop that also keeps dispatching
 * dwl's own Wayland event loop (e.g. wl_event_loop_dispatch()) between
 * polls -- after wl_display_terminate(), nothing else does that any more,
 * and the tray thread may be blocked waiting for exactly that. */
int awl_tray_join(void);

/* Rebuilds every monitor's SNI Host (D-Bus StatusNotifierHost registration
 * and item list) from scratch, in place, without touching the tray's GTK
 * thread, windows, or the process-wide StatusNotifierWatcher registry --
 * only each Host's own mirror of the watcher's item list is discarded and
 * re-fetched. Recovers from a Host's D-Bus state going stale (observed
 * after suspend+wake, where icons simply stop appearing) without the
 * caller needing to block: unlike awl_tray_shutdown()/_init(), this is
 * fire-and-forget -- safe to call from any thread, asynchronous, no
 * awl_tray_join()-style polling needed. (Recreating the tray thread itself
 * to "reload" is not an option: gtkmm's Gtk::Main object cannot safely be
 * constructed a second time after a previous one in the same process was
 * destroyed -- its global type-wrapper registration tables don't come
 * back, and the next D-Bus proxy object the tray tries to wrap crashes.)
 * Wire this into whatever already re-runs on wake/suspend recovery, e.g.
 * alongside awl_plugin_restart(). No-op if the tray was never started. */
void awl_tray_reload(void);

/* All of the below identify which monitor's tray overlay window they apply
 * to via `monitor_id`, a stable per-output identifier -- pass
 * m->wlr_output->name. Each monitor gets its own independent overlay
 * window (and its own SNI host/item set, mirroring the same real tray
 * items), created lazily the first time any call below mentions a given
 * monitor_id. */

/* Current width of the given monitor's tray overlay window content, in the
 * same buffer-scaled pixel space as the bar's own pixman buffer -- mirrors
 * widget_t.draw()'s return-value contract, letting dwl reserve the right
 * amount of space in that monitor's bar layout even though the tray
 * renders itself. */
uint32_t awl_tray_width(const char *monitor_id);

/* Tells the given monitor's tray where dwl is about to place the systray
 * widget in its bar this frame, in buffer-scaled pixels relative to the
 * bar's own left edge. Combined with awl_tray_set_bar_geometry()'s
 * monitor-space origin, this auto-positions the real overlay window over
 * that slot every frame instead of relying on hardcoded margins. Call from
 * systray_draw(). */
void awl_tray_set_widget_x(const char *monitor_id, uint32_t x);

/* Keeps the given monitor's tray overlay window positioned/sized to track
 * its real bar. `x`/`y`/`width`/`height` are the bar's rect in the same
 * layout-relative coordinates dwl positions the bar's own scene buffer
 * in. */
void awl_tray_set_bar_geometry(const char *monitor_id, int32_t x, int32_t y, uint32_t width, uint32_t height, double scale);

/* Shows or hides the given monitor's tray overlay window. The tray is a
 * real, separate layer-shell surface, not something drawn into the bar's
 * own buffer, so hiding the bar (m->showbar) doesn't hide it on its own --
 * call this from wherever dwl toggles bar visibility for a monitor. */
void awl_tray_set_visible(const char *monitor_id, int visible);

/* Tears down the given monitor's tray overlay window (if one was ever
 * created for it) -- call when a monitor is being destroyed (e.g.
 * unplugged), from cleanupmon(), before its wlr_output goes away. Safe to
 * call even if no window was ever created for this monitor_id. */
void awl_tray_remove_monitor(const char *monitor_id);

#ifdef __cplusplus
}
#endif
