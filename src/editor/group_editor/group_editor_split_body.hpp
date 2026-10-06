#pragma once
#include "group_editor.hpp"

namespace dune3d {

class GroupSplitBody;

class GroupEditorSplitBody : public GroupEditor {
public:
    GroupEditorSplitBody(Core &core, const UUID &group_uu);

    void do_reload() override;

private:
    GroupSplitBody &get_group();

    bool update();

    Gtk::SpinButton *m_px_sp = nullptr;
    Gtk::SpinButton *m_py_sp = nullptr;
    Gtk::SpinButton *m_pz_sp = nullptr;
    Gtk::SpinButton *m_nx_sp = nullptr;
    Gtk::SpinButton *m_ny_sp = nullptr;
    Gtk::SpinButton *m_nz_sp = nullptr;
};

} // namespace dune3d
