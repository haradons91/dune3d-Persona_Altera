#pragma once
#include "group_editor_face_operation.hpp"

namespace dune3d {

class GroupShell;

class GroupEditorShell : public GroupEditorFaceOperation {
public:
    GroupEditorShell(Core &core, const UUID &group_uu);

    void do_reload() override;

protected:
    void construct_extra() override;

private:
    GroupShell &get_group_shell();

    bool update_thickness();

    Gtk::SpinButton *m_thickness_sp = nullptr;
};

} // namespace dune3d
