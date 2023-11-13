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
    win.add( box_ );
    if (!id.empty()) box_.get_style_context()->add_class(id);
    nb_hosts_ += 1;
    dp_.emit();
}

void Tray::onAdd(std::unique_ptr<Item>& item) {
    box_.pack_start(item->event_box);
    dp_.emit();
}

void Tray::onRemove(std::unique_ptr<Item>& item) {
    box_.remove(item->event_box);
    dp_.emit();
}

void Tray::update() {
    box_.set_visible(1);
    /* box_.set_visible(!box_.get_children().empty()); */
    dp_.emit();
}

}  // namespace SNI
