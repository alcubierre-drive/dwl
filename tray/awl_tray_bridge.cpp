// Bridges dwl's bar (pure C, wlroots render thread) to the SNI tray logic
// in tray.hpp/host.hpp/item.hpp (C++, glib/gtk thread). Owns a dedicated
// pthread running a GTK/GLib main loop that hosts a real, input-owning
// gtk-layer-shell overlay window containing SNI::Tray's box of icons.
//
// This is deliberately close to the tray's original standalone design
// (a real GtkWindow that GTK renders and Wayland delivers input to
// directly, so context-menu popups -- which need a genuine input-event
// serial -- work exactly as libdbusmenu-gtk expects). The two things that
// change from the original: it runs as a library thread with a lifecycle
// dwl owns (awl_tray_init()/awl_tray_shutdown() from dwl.c's run()/
// cleanup()) instead of being spawned as a separate process, and it's
// auto-positioned from dwl's real bar geometry/layout (awl_tray_set_bar_geometry
// from updatebar(), awl_tray_set_widget_x() from the systray widget's own
// draw call) instead of hardcoded gtk-layer-shell margins.
//
// dwl's render thread never touches GTK/D-Bus directly: it calls
// awl_tray_width()/_set_widget_x()/_set_bar_geometry(), which only touch a
// few atomics/a small mutex, or marshal work onto the GTK thread via
// g_idle_add() (documented thread-safe).

#include "awl_tray.h"
#include "tray.hpp"

#include <gtk-layer-shell.h>
#include <gtkmm.h>

#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace SNI {
namespace {

struct BarGeom {
  int32_t x = 0, y = 0;
  int32_t width = 0, height = 20;
  double scale = 1.0;
};

// dwl calls awl_tray_set_bar_geometry() from updatebar(), which runs at
// monitor-creation time -- potentially before this thread's Gtk::Main has
// finished its (deliberately non-blocking, see awl_tray_init()) Wayland
// connection handshake and this monitor's Bridge has been constructed (see
// ensure_bridge()). If that first call lands while the Bridge doesn't exist
// yet, it would otherwise be silently dropped: updatebar() only re-fires on
// real monitor/output changes, not every frame, so a static session might
// never send geometry again for that monitor, leaving its Bridge stuck at
// BarGeom's all-zero default forever (X positioning masks this, since
// awl_tray_set_widget_x() -- and thus a reposition() retry -- is called
// every frame from systray_draw(), but Y never gets fixed up because
// nothing keeps re-sending geometry the same way). Cache the latest call
// per monitor here, independent of that monitor's Bridge readiness, so
// Bridge's constructor can pick up whatever was last sent instead of only
// ever seeing pushes that happened to land after it existed.
std::mutex g_pending_geom_mtx;
std::unordered_map<std::string, BarGeom> g_pending_geom;

// Marshals `fn` onto the tray's GTK/GLib thread. Safe to call from any
// thread (g_idle_add is documented thread-safe).
template <typename F>
void run_on_gtk_thread(F &&fn) {
  auto *boxed = new std::function<void()>(std::forward<F>(fn));
  g_idle_add_full(
      G_PRIORITY_DEFAULT,
      [](gpointer data) -> gboolean {
        auto *f = static_cast<std::function<void()> *>(data);
        (*f)();
        delete f;
        return G_SOURCE_REMOVE;
      },
      boxed, nullptr);
}

class Bridge {
 public:
  explicit Bridge(std::string monitor_id);
  ~Bridge();

  void setBarGeometry(int32_t x, int32_t y, int32_t w, int32_t h, double scale);
  void setWidgetX(uint32_t x);
  void setVisible(bool visible);
  void reloadTray();
  uint32_t width();

 private:
  void reposition();

  std::string monitor_id_;
  std::unique_ptr<Gtk::Window> win_;
  std::unique_ptr<Tray> tray_;
  // The width-repoll timer below runs for as long as this connection is
  // alive -- it captures `this` in its lambda, so it MUST be disconnected
  // in ~Bridge() before the Bridge itself goes away. Without this, the
  // still-armed GLib timeout source outlives the Bridge (destroying a
  // Bridge doesn't implicitly cancel timers it registered) and its next
  // 200ms tick dereferences a freed `this` -- a use-after-free that's
  // silent until that memory happens to get reused, which is why it only
  // crashed intermittently around monitor disconnects/tray reloads.
  sigc::connection poll_conn_;

  std::mutex geom_mtx_;
  BarGeom bar_geom_;
  std::atomic<int32_t> widget_x_{0};   // buffer-scaled px, from systray_draw's `x`
  std::atomic<int32_t> content_w_{0};  // logical px, box_'s current allocated width

  int last_margin_left_ = INT32_MIN;
  int last_margin_top_ = INT32_MIN;
  int last_height_ = -1;
  int last_forced_w_ = -1;
  bool shown_ = false;
  // Whether the monitor bar we're tracking currently wants us visible (dwl's
  // togglebar). Only touched on the GTK thread (via run_on_gtk_thread), same
  // as shown_/reposition()'s other state.
  bool visible_ = true;
};

Bridge::Bridge(std::string monitor_id) : monitor_id_(std::move(monitor_id)) {
  // Pick up whatever geometry dwl already tried to send for this monitor
  // before its Bridge was ready (see the comment on g_pending_geom) instead
  // of only starting from BarGeom's all-zero default.
  {
    std::lock_guard<std::mutex> lg(g_pending_geom_mtx);
    std::lock_guard<std::mutex> lg2(geom_mtx_);
    auto it = g_pending_geom.find(monitor_id_);
    if (it != g_pending_geom.end()) bar_geom_ = it->second;
  }

  win_ = std::make_unique<Gtk::Window>();
  win_->set_decorated(false);
  win_->set_name("awl-tray");

  gtk_layer_init_for_window(win_->gobj());
  // The namespace encodes which monitor this window belongs to as
  // "awl-tray:<monitor_id>" -- dwl.c's createlayersurface() parses this
  // prefix and binds the surface to that exact wlr_output before falling
  // back to its normal "no output requested -> selmon" default. Without
  // this, every tray window (one per monitor, all requesting no specific
  // output) would land on whatever selmon happens to be, so only one
  // monitor would ever show a tray, and its window would fight between
  // monitors' geometry pushes (each monitor's own bar redraw calls
  // awl_tray_set_bar_geometry()/_set_widget_x() for its own Bridge now, so
  // that fight no longer exists once each is correctly bound to its own
  // output -- but binding is what actually prevents it).
  gtk_layer_set_namespace(win_->gobj(), ("awl-tray:" + monitor_id_).c_str());
  // BOTTOM, not TOP: dwl's own bar lives in its LyrBottom scene layer (see
  // the comment on m->scene_buffer's creation in dwl.c), which sits below
  // floating windows by design -- the tray should stay in sync with that,
  // not float above it, so a floating window dragged over the bar covers
  // both together instead of just the bar.
  gtk_layer_set_layer(win_->gobj(), GTK_LAYER_SHELL_LAYER_BOTTOM);
  gtk_layer_set_keyboard_interactivity(win_->gobj(), false);
  gtk_layer_set_anchor(win_->gobj(), GTK_LAYER_SHELL_EDGE_LEFT, true);
  gtk_layer_set_anchor(win_->gobj(), GTK_LAYER_SHELL_EDGE_TOP, true);
  // dwl's own bar widget layout already reserves the horizontal space for
  // us (systray_draw's return value), so the layer surface itself must not
  // additionally reserve compositor exclusive zone space.
  gtk_layer_set_exclusive_zone(win_->gobj(), 0);

  Gdk::RGBA bg("#859394");
  win_->override_background_color(bg);

  tray_ = std::make_unique<Tray>("", *win_);
  // The original main.cpp called this right after constructing Tray;
  // Tray::update() is what actually makes box_ visible in the first place
  // (box_.set_visible(1)) -- without it box_ stays invisible and
  // contributes nothing to any preferred-size computation no matter what
  // icons it holds.
  tray_->update();
  // Tray/Item are unmodified original code with no opinion on vertical
  // alignment; win_ is a GtkBin so its single child (box_) stretch-fills
  // by default. Since our window is pinned to the bar's real height (which
  // can be taller than a single row of icons), center the row within it
  // instead of letting it stretch and (depending on how its children's own
  // internal allocation ends up sized) potentially clip/misplace icons.
  tray_->box_.set_valign(Gtk::ALIGN_CENTER);

  // GtkWindow only auto-sizes to its content's natural size up to the first
  // time it's actually resized (which happens as soon as the window is
  // first mapped, in reposition() below); after that it keeps whatever
  // size it was last given and won't grow/shrink on its own as icons come
  // and go, and (for reasons that didn't reproduce with a plain GTK
  // container in isolation) box_'s own size-allocate signal never fires
  // here to hook a reaction off of. Poll the box's natural width instead
  // and force a fresh renegotiation whenever it changes. (Forcing this
  // unconditionally on every tick, instead of only on real changes, was
  // tried as a way to retry updateImage() for items that set their image
  // before the window was realized -- see Item::updateImage()'s window-null
  // fallback for the actual fix for that -- but constant forced
  // reconfiguration interfered with the compositor's own frame/screencopy
  // scheduling, so it's change-gated again here.)
  //
  // Deliberately resize to (nat_w, last_height_) rather than gtk-layer-shell's
  // documented "resize(1, 1) to snap back to natural size" recipe: some
  // icons' natural/preferred height (e.g. before their image widget is
  // realized) can exceed the bar's real height, and resize(1, 1) lets
  // GTK grow *both* axes to fit that, overflowing the window past the bar
  // strip. Pinning height explicitly on every forced resize keeps the
  // window clipped to the bar's actual height no matter what a child asks
  // for, matching the old hardcoded-size behavior for that axis while
  // still tracking width dynamically.
  poll_conn_ = Glib::signal_timeout().connect(
      [this]() -> bool {
        if (!win_ || !tray_) return false;
        int min_w = 0, nat_w = 0;
        tray_->box_.get_preferred_width(min_w, nat_w);
        content_w_.store(nat_w, std::memory_order_relaxed);
        if (shown_ && nat_w != last_forced_w_) {
          last_forced_w_ = nat_w;
          int w = nat_w > 0 ? nat_w : 1;
          int h = last_height_ > 0 ? last_height_ : 1;
          // resize() alone is only a hint here and was observed to not
          // actually change this undecorated layer-shell window's real
          // GTK allocation once it had already been mapped with a smaller
          // width (verified via [poll-debug]: nat_w/last_forced_w tracked
          // the icon's real width correctly, but win_->get_allocation()
          // stayed stuck at width 1 forever). set_size_request() is a hard
          // constraint on the widget's size, not just a hint, and reliably
          // forces the reallocation that resize() alone didn't.
          win_->set_size_request(w, h);
          win_->resize(w, h);
        }
        return true;
      },
      200);

  // Deliberately not shown yet -- see reposition(): the window is first
  // mapped only once real bar geometry is known, so gtk-layer-shell's
  // initial size negotiation reflects the real bar height instead of
  // GTK's un-negotiated 200x200 fallback default.
}

Bridge::~Bridge() {
  // Must happen before tray_/win_ are torn down: the timer lambda captures
  // `this` and runs on this same GTK thread, so once we're in ~Bridge()
  // it can no longer fire concurrently -- but it's still an armed GLib
  // source until explicitly disconnected, and would otherwise dereference
  // this freed Bridge on its next 200ms tick. See poll_conn_'s comment.
  poll_conn_.disconnect();
  tray_.reset();
  win_.reset();
}

void Bridge::reposition() {
  if (!win_) return;
  BarGeom g;
  int32_t wx;
  {
    std::lock_guard<std::mutex> lg(geom_mtx_);
    g = bar_geom_;
  }
  wx = widget_x_.load(std::memory_order_relaxed);

  // gtk-layer-shell margins get added by wlroots *directly* onto the
  // compositor's own output-layout coordinates (wlr_scene_layer_surface_v1_configure
  // adds margin straight onto full_area, i.e. m->m -- dwl's own logical-pixel
  // space, physical / dwl's fractional output scale) -- confirmed by
  // instrumenting dwl.c's arrangelayer() directly and comparing its
  // resulting scene position against what was sent here. GTK's own
  // negotiated client-side scale (the legacy integer wl_output.scale
  // gtk-layer-shell sees) plays no part in that placement -- it only
  // matters for GTK's own rendering, e.g. converting box_'s preferred
  // width to physical pixels in width() below. So g.x/g.y/g.height (already
  // dwl-logical, from updatebar() in dwl.c) need no conversion at all here;
  // only wx -- physical/"buffer-scaled" per its documented contract, since
  // it comes from the bar's own pixman-buffer x coordinate -- needs
  // dividing by dwl's own scale to land in that same dwl-logical space.
  int margin_left = g.x + (int)(g.scale > 0 ? wx / g.scale : wx);
  int margin_top = g.y;
  int height = std::max(1, g.height);

  if (margin_left != last_margin_left_) {
    gtk_layer_set_margin(win_->gobj(), GTK_LAYER_SHELL_EDGE_LEFT, margin_left);
    last_margin_left_ = margin_left;
  }
  if (margin_top != last_margin_top_) {
    gtk_layer_set_margin(win_->gobj(), GTK_LAYER_SHELL_EDGE_TOP, margin_top);
    last_margin_top_ = margin_top;
  }
  if (height != last_height_) {
    // Use last_forced_w_ here too (not -1/unconstrained) -- set_size_request()
    // is what actually forces this window's real GTK allocation (see the
    // poll timer's comment), so resetting the width constraint to -1 on
    // every height change would silently undo whatever width the poll timer
    // had already pinned, snapping the window back down to its unconstrained
    // (effectively 1px) width until the next poll tick catches up.
    int w = last_forced_w_ > 0 ? last_forced_w_ : 1;
    win_->set_size_request(w, height);
    last_height_ = height;
    // Re-pin height immediately (see the poll timer in the constructor for
    // why an explicit height is used instead of gtk-layer-shell's
    // resize(1, 1) recipe). Width tracking is left to the poll timer.
    if (shown_) win_->resize(w, height);
  }

  // First real geometry: map the window now, with the correct min-height
  // size request already in place (see comment in the constructor) -- but
  // only if the bar we're tracking isn't currently hidden (see setVisible()).
  if (!shown_ && visible_) {
    win_->show();
    shown_ = true;
  }
}

void Bridge::setBarGeometry(int32_t x, int32_t y, int32_t w, int32_t h, double scale) {
  bool moved;
  {
    std::lock_guard<std::mutex> g(geom_mtx_);
    moved = bar_geom_.x != x || bar_geom_.y != y || bar_geom_.width != w ||
            bar_geom_.height != h || bar_geom_.scale != scale;
    bar_geom_.x = x;
    bar_geom_.y = y;
    bar_geom_.width = w;
    bar_geom_.height = h;
    bar_geom_.scale = scale;
  }
  if (moved) run_on_gtk_thread([this] { reposition(); });
}

void Bridge::setWidgetX(uint32_t x) {
  int32_t prev = widget_x_.exchange((int32_t)x, std::memory_order_relaxed);
  if (prev != (int32_t)x) run_on_gtk_thread([this] { reposition(); });
}

void Bridge::setVisible(bool visible) {
  run_on_gtk_thread([this, visible] {
    visible_ = visible;
    // Not mapped yet (e.g. toggled before the first real bar geometry ever
    // arrived) -- nothing to show/hide; reposition() checks visible_ itself
    // once it does map the window for the first time.
    if (!win_ || !shown_) return;
    if (visible) {
      win_->show();
    } else {
      win_->hide();
    }
  });
}

void Bridge::reloadTray() {
  if (!win_ || !tray_) return;
  // Keep the process-wide Watcher singleton (SNI::Watcher, which actually
  // owns the org.kde.StatusNotifierWatcher bus name every tray app on the
  // system registers its icon with) alive across the swap below,
  // independent of whichever Tray happens to hold the last strong
  // reference to it at any given instant. Without this, on a session with
  // only one monitor (one Bridge, one Tray), resetting tray_ below before
  // constructing its replacement would drop Watcher's refcount to zero and
  // momentarily tear down that bus-name ownership along with the one Host
  // actually being reloaded.
  auto keep_watcher_alive = Watcher::getInstance();

  // A GtkWindow (GtkBin) only ever holds one child -- explicitly detach the
  // old Tray's box_ before destroying it, so the new Tray's own win.add()
  // in its constructor below doesn't warn/fail against a still-attached
  // stale child. (Gtk::Bin::remove() takes no argument: a Bin only ever has
  // the one child anyway.)
  win_->remove();
  tray_.reset();
  tray_ = std::make_unique<Tray>("", *win_);
  tray_->update();
  tray_->box_.set_valign(Gtk::ALIGN_CENTER);
}

uint32_t Bridge::width() {
  int32_t w = content_w_.load(std::memory_order_relaxed);
  if (w <= 0) return 0;
  // content_w_ is box_'s preferred width in *this window's own* GTK-logical
  // pixels; dwl wants it back in units matching its own raw bar buffer (see
  // drawbar()'s m->b.width, which is m->b.real_width * m->wlr_output->scale)
  // to reserve bar space. That's dwl's own *real* output scale (bar_geom_.scale,
  // e.g. 1.5) -- the same one reposition() uses for margins, per its comment
  // on wlr_scene_layer_surface_v1_configure(). It is NOT this window's own
  // client-negotiated buffer scale (win_->get_scale_factor()): GTK3/
  // gtk-layer-shell only understands the legacy integer wl_output.scale,
  // which wlroots reports as ceil(1.5)=2 for a 1.5x monitor -- using that
  // here over-reserved bar space by 2/1.5 (~33%), leaving a visible
  // transparent gap past the real icons' right edge.
  double scale;
  {
    std::lock_guard<std::mutex> lg(geom_mtx_);
    scale = bar_geom_.scale;
  }
  if (scale <= 0) scale = 1.0;
  return (uint32_t)(w * scale + 0.5);
}

// One Bridge (one overlay window) per monitor, keyed by monitor_id
// (m->wlr_output->name). Only ever constructed/destroyed on the GTK
// thread (inside ensure_bridge()'s/awl_tray_remove_monitor()'s
// run_on_gtk_thread() callback, or thread_main()'s final teardown below);
// the mutex just protects the map's own structure (insert/erase/find) from
// the render thread's concurrent lookups, not the Bridge objects'
// internals (those are already safe for cross-thread use on their own,
// same as before this was a map).
std::mutex g_bridges_mtx;
std::unordered_map<std::string, std::unique_ptr<Bridge>> g_bridges;

// Returns the existing Bridge for `mon`, or nullptr if none exists (yet).
// Safe to call from any thread.
Bridge *find_bridge(const std::string &mon) {
  std::lock_guard<std::mutex> lg(g_bridges_mtx);
  auto it = g_bridges.find(mon);
  return (it == g_bridges.end() || !it->second) ? nullptr : it->second.get();
}

// Creates a Bridge for `mon` if one doesn't already exist (or isn't
// already being created), asynchronously on the GTK thread -- Bridge's
// constructor touches Gtk::Window/gtk-layer-shell, which must happen
// there. Safe to call repeatedly/from any thread; idempotent.
void ensure_bridge(const std::string &mon) {
  {
    std::lock_guard<std::mutex> lg(g_bridges_mtx);
    if (g_bridges.count(mon)) return;
    // Reserve the slot (as a null entry) immediately so a burst of calls
    // for the same brand-new monitor_id (e.g. width() and
    // set_bar_geometry() both firing the same frame) only queues one
    // construction, instead of racing multiple Bridges into the same slot.
    g_bridges.emplace(mon, nullptr);
  }
  run_on_gtk_thread([mon] {
    std::lock_guard<std::mutex> lg(g_bridges_mtx);
    auto &slot = g_bridges[mon];
    if (!slot) slot = std::make_unique<Bridge>(mon);
  });
}

pthread_t g_thread;
std::atomic<bool> g_running{false};
// Set by thread_main right before it returns, i.e. once Gtk::Main::run()
// has actually come back and all Bridges have been torn down -- see
// awl_tray_join()'s comment for why the caller polls this instead of just
// pthread_join()-ing directly.
std::atomic<bool> g_finished{false};

// Runs on its own thread. Gtk::Main's constructor connects to us as a
// Wayland client, which can only complete once dwl's own main thread
// reaches wl_display_run() -- this blocks here (harmlessly, on this
// background thread) until then rather than the caller of awl_tray_init().
void *thread_main(void *) {
  int argc = 1;
  static char prog_name[] = "awltray";
  static char *argv0 = prog_name;
  char **argv = &argv0;
  Gtk::Main kit(argc, argv);

  // Bridges are created lazily, per monitor, the first time dwl mentions a
  // monitor_id (see ensure_bridge()) -- there's no single "the" tray window
  // to construct eagerly here any more.

  Gtk::Main::run();

  // Explicitly destroy every Bridge (and with it each one's win_/tray_,
  // i.e. this thread's own Wayland client state -- window/surface
  // destruction, final object cleanup) *before* signalling g_finished, so
  // the dwl-side poll loop in awl_tray_join() can't observe "finished"
  // while that teardown is still in flight (which would let cleanup()
  // race ahead into wl_display_destroy_clients()/wl_display_destroy()
  // concurrently with this thread still using the display).
  {
    std::lock_guard<std::mutex> lg(g_bridges_mtx);
    g_bridges.clear();
  }
  g_finished.store(true, std::memory_order_release);
  return nullptr;
}

}  // namespace
}  // namespace SNI

extern "C" {

void awl_tray_init(void) {
  if (SNI::g_running.load()) return;
  SNI::g_running = true;
  pthread_create(&SNI::g_thread, nullptr, SNI::thread_main, nullptr);
}

void awl_tray_shutdown(void) {
  if (!SNI::g_running.load()) return;
  // Deliberately does NOT pthread_join() here: by the time dwl's cleanup()
  // calls this, wl_display_terminate() has already made wl_display_run()
  // return, so nothing is dispatching dwl's own Wayland event loop any
  // more. If the tray's GTK thread needs a response from us as the server
  // to finish tearing down its Wayland client connection (a frame
  // callback, a layer-surface configure ack, a buffer release -- any of
  // which can happen during window/GTK teardown), a blocking join here
  // would deadlock forever waiting for an answer nobody is left to send.
  // See awl_tray_join(), which the caller must poll instead while still
  // pumping its own event loop.
  SNI::run_on_gtk_thread([] { Gtk::Main::quit(); });
}

int awl_tray_join(void) {
  if (!SNI::g_running.load()) return 1;
  if (!SNI::g_finished.load(std::memory_order_acquire)) return 0;
  pthread_join(SNI::g_thread, nullptr);
  SNI::g_running = false;
  return 1;
}

uint32_t awl_tray_width(const char *monitor_id) {
  std::string mon(monitor_id ? monitor_id : "");
  SNI::ensure_bridge(mon);
  SNI::Bridge *b = SNI::find_bridge(mon);
  return b ? b->width() : 0;
}

void awl_tray_set_widget_x(const char *monitor_id, uint32_t x) {
  std::string mon(monitor_id ? monitor_id : "");
  SNI::ensure_bridge(mon);
  SNI::Bridge *b = SNI::find_bridge(mon);
  if (b) b->setWidgetX(x);
}

void awl_tray_set_bar_geometry(const char *monitor_id, int32_t x, int32_t y, uint32_t width, uint32_t height, double scale) {
  std::string mon(monitor_id ? monitor_id : "");
  {
    std::lock_guard<std::mutex> lg(SNI::g_pending_geom_mtx);
    SNI::g_pending_geom[mon] = SNI::BarGeom{x, y, (int32_t)width, (int32_t)height, scale};
  }
  SNI::ensure_bridge(mon);
  SNI::Bridge *b = SNI::find_bridge(mon);
  if (b) b->setBarGeometry(x, y, (int32_t)width, (int32_t)height, scale);
}

void awl_tray_reload(void) {
  if (!SNI::g_running.load()) return;
  SNI::run_on_gtk_thread([] {
    std::lock_guard<std::mutex> lg(SNI::g_bridges_mtx);
    for (auto &kv : SNI::g_bridges) {
      if (kv.second) kv.second->reloadTray();
    }
  });
}

void awl_tray_set_visible(const char *monitor_id, int visible) {
  std::string mon(monitor_id ? monitor_id : "");
  SNI::Bridge *b = SNI::find_bridge(mon);
  if (b) b->setVisible(visible != 0);
}

void awl_tray_remove_monitor(const char *monitor_id) {
  std::string mon(monitor_id ? monitor_id : "");
  {
    std::lock_guard<std::mutex> lg(SNI::g_pending_geom_mtx);
    SNI::g_pending_geom.erase(mon);
  }
  if (!SNI::g_running.load()) return;
  SNI::run_on_gtk_thread([mon] {
    std::lock_guard<std::mutex> lg(SNI::g_bridges_mtx);
    SNI::g_bridges.erase(mon);
  });
}

}  // extern "C"
