#pragma once

#include <gtkmm.h>
#include <gtkmm/box.h>
#include "host.hpp"
#include "watcher.hpp"

namespace SNI {

class Tray {
 public:
  Tray(const std::string&, Gtk::Window& win);
  virtual ~Tray() = default;
  void update();

  Gtk::Box box_;
 private:
  void onAdd(std::unique_ptr<Item>& item);
  void onRemove(std::unique_ptr<Item>& item);

  static inline std::size_t nb_hosts_ = 0;
  SNI::Watcher::singleton watcher_;
  SNI::Host host_;
  Glib::Dispatcher dp_;
};

}  // namespace SNI
