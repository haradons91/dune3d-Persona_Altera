#pragma once
#include "group_editor_face_operation.hpp"

namespace dune3d {

class GroupDraft;

class GroupEditorDraft : public GroupEditorFaceOperation {
public:
    GroupEditorDraft(Core &core, const UUID &group_uu);

    void do_reload() override;

protected:
    void construct_extra() override;

private:
    GroupDraft &get_group_draft();

    bool update_angle();

    Gtk::SpinButton *m_angle_sp = nullptr;
};

} // namespace dune3d
