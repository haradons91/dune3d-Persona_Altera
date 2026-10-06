#pragma once
#include "group_editor_face_operation.hpp"

namespace dune3d {

class GroupSplitFace;

class GroupEditorSplitFace : public GroupEditorFaceOperation {
public:
    GroupEditorSplitFace(Core &core, const UUID &group_uu);

    void do_reload() override;

protected:
    void construct_extra() override;

private:
    GroupSplitFace &get_group_split_face();

    bool update();

    Gtk::SpinButton *m_px_sp = nullptr;
    Gtk::SpinButton *m_py_sp = nullptr;
    Gtk::SpinButton *m_pz_sp = nullptr;
    Gtk::SpinButton *m_nx_sp = nullptr;
    Gtk::SpinButton *m_ny_sp = nullptr;
    Gtk::SpinButton *m_nz_sp = nullptr;
};

} // namespace dune3d
