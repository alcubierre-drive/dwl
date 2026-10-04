#pragma once

#include <string>

// A small month calendar popup (layer-shell window) listing the events of
// the selected day from evolution-data-server -- i.e. the same calendars
// GNOME Online Accounts / gnome-calendar / evolution use. Read-only; buttons
// hand off to gnome-calendar and evolution for anything else. Built without
// libecal-2.0 (no AWL_HAVE_ECAL) it is a plain calendar without events.
//
// Everything here runs on the tray's GTK thread (awl_tray_bridge.cpp).

namespace awl {

// Creates the (hidden) popup and starts connecting to EDS in the
// background, so the first toggle doesn't have to wait for it.
void calendar_init();
// Destroys the popup and drops the EDS connections; waits for a running
// query to finish (it is cancelled first).
void calendar_fini();
// Shows the popup on `monitor_id` (anchored at the right end of the bar,
// on the bar's side of the screen) or hides it if it is already shown there.
void calendar_toggle(const std::string& monitor_id, bool bar_on_top);
void calendar_hide();

}  // namespace awl
