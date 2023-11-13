#include "gtk_icon.hpp"
#include "tray.hpp"
#include <gtk-layer-shell.h>

class TrayWindow : public Gtk::Window {
    public:
        TrayWindow() {
            tray_ = new SNI::Tray("awl-tray", *this);
            gtk_layer_init_for_window(this->gobj());
            gtk_layer_set_keyboard_interactivity(this->gobj(), false);
            gtk_layer_set_layer(this->gobj(), GTK_LAYER_SHELL_LAYER_TOP);
            gtk_layer_set_anchor(this->gobj(), GTK_LAYER_SHELL_EDGE_RIGHT, true);
            gtk_layer_set_anchor(this->gobj(), GTK_LAYER_SHELL_EDGE_BOTTOM, true);
            /* gtk_layer_set_exk */
            gtk_layer_set_exclusive_zone(this->gobj(), 0);
            gtk_layer_set_margin(this->gobj(), GTK_LAYER_SHELL_EDGE_BOTTOM, -21);
            gtk_layer_set_margin(this->gobj(), GTK_LAYER_SHELL_EDGE_RIGHT, 66);
            this->set_size_request(64,20);
            Gdk::RGBA C("#859394");
            this->override_background_color(C);

            /* gdk_window_hide(this->get_window()->gobj()); */
            /* set_decorated(false); */
            /* set_name("waybar"); */
            /* set_title("waybar"); */
            tray_->update();
        }
        ~TrayWindow() {
            delete tray_;
        }
    private:
        SNI::Tray* tray_;
};
int main(int argc, char** argv) {
    Gtk::Main main_obj(argc, argv);
    TrayWindow window_obj; // {Gtk::WindowType::WINDOW_TOPLEVEL};

    main_obj.run(window_obj);
}
