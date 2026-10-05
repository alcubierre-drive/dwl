#pragma once

#include <gtkmm.h>
#include <gtkmm/box.h>
#include <functional>
#include "host.hpp"
#include "watcher.hpp"

namespace SNI {

class Tray {
public:
    Tray(const std::string&, Gtk::Window& win);
    virtual ~Tray() = default;
    void update();

    // Called (on the GTK thread) whenever something that can change box_'s
    // preferred width happens: an item is added/removed, an item's visibility
    // (Status) changes, or an item's image is replaced (icon/size/scale change).
    // Set by the owner (awl_tray_bridge.cpp's Bridge) right after construction.
    // Declared before box_/host_ on purpose: members are destroyed in reverse
    // order, so this outlives host_'s Items (whose widget signal handlers call
    // it, possibly during their own destruction).
    std::function<void()> on_change_;
    Gtk::Box box_;
private:
    void onAdd(std::unique_ptr<Item>& item);
    void onRemove(std::unique_ptr<Item>& item);
    void notifyChange();
    static void onItemWidgetNotify(GObject*, GParamSpec*, gpointer self);

    static inline std::size_t nb_hosts_ = 0;
    SNI::Watcher::singleton watcher_;
    SNI::Host host_;
    Glib::Dispatcher dp_;
};

}  // namespace SNI
