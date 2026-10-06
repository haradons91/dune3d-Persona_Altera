#pragma once
#include "group_editor.hpp"

namespace dune3d {

class GroupMoveCopy;

class GroupEditorMoveCopy : public GroupEditor {
public:
    GroupEditorMoveCopy(Core &core, const UUID &group_uu);

    void do_reload() override;

private:
    GroupMoveCopy &get_group();

    bool update();

    Gtk::SpinButton *m_tx_sp = nullptr;
    Gtk::SpinButton *m_ty_sp = nullptr;
    Gtk::SpinButton *m_tz_sp = nullptr;
    Gtk::SpinButton *m_ax_sp = nullptr;
    Gtk::SpinButton *m_ay_sp = nullptr;
    Gtk::SpinButton *m_az_sp = nullptr;
    Gtk::SpinButton *m_angle_sp = nullptr;
    Gtk::CheckButton *m_copy_cb = nullptr;
};

} // namespace dune3d
