#include "tray.hpp"

#include <gtkmm/window.h>

namespace SNI {

Tray::Tray(const std::string& id, Gtk::Window& win)
    : box_(Gtk::ORIENTATION_HORIZONTAL, 0),
      watcher_(SNI::Watcher::getInstance()),
      host_(nb_hosts_,
            std::bind(&Tray::onAdd, this, std::placeholders::_1),
            std::bind(&Tray::onRemove, this, std::placeholders::_1),
            win) {
    box_.set_name("tray");
    box_.set_margin_start(4);
    box_.set_margin_end(4);
    box_.set_spacing(4);
    win.add( box_ );
    if (!id.empty()) box_.get_style_context()->add_class(id);
    nb_hosts_ += 1;
}

void Tray::notifyChange() {
    if (on_change_) on_change_();
}

void Tray::onItemWidgetNotify(GObject*, GParamSpec*, gpointer self) {
    static_cast<Tray*>(self)->notifyChange();
}

void Tray::onAdd(std::unique_ptr<Item>& item) {
    box_.pack_start(item->event_box);
    // Width-relevant per-item changes: Item::setStatus() toggles event_box's
    // visibility, Item::updateImage() replaces image's pixbuf/surface (new
    // icon, icon_size or scale factor). Both handlers live on the Item's own
    // widgets and die with them; `this` (the Tray) owns host_, which owns
    // the Items, so the Tray strictly outlives every handler connected here.
    g_signal_connect(item->event_box.gobj(), "notify::visible",
                     G_CALLBACK(&Tray::onItemWidgetNotify), this);
    g_signal_connect(item->image.gobj(), "notify",
                     G_CALLBACK(&Tray::onItemWidgetNotify), this);
    notifyChange();
}

void Tray::onRemove(std::unique_ptr<Item>& item) {
    box_.remove(item->event_box);
    notifyChange();
}

void Tray::update() {
    box_.set_visible(1);
    /* box_.set_visible(!box_.get_children().empty()); */
}

}  // namespace SNI
