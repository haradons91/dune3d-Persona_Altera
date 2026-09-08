#pragma once
#include <gtkmm.h>

namespace dune3d {
class SpinButtonDim : public Gtk::SpinButton {
public:
    SpinButtonDim();
    void set_decimal_places(int places) { m_decimal_places = places; }
    void set_fit_content(bool fit) { m_fit_content = fit; }

protected:
    int on_input(double &new_value);
    bool on_output();
    int m_decimal_places = -1;
    bool m_fit_content = false;
    // void on_populate_popup(Gtk::Menu *menu) override;
};
} // namespace dune3d
