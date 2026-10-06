#pragma once
#include "group_editor_face_operation.hpp"

namespace dune3d {

class GroupOffsetFace;

class GroupEditorOffsetFace : public GroupEditorFaceOperation {
public:
    GroupEditorOffsetFace(Core &core, const UUID &group_uu);

    void do_reload() override;

protected:
    void construct_extra() override;

private:
    GroupOffsetFace &get_group_offset_face();

    bool update_offset();

    Gtk::SpinButton *m_offset_sp = nullptr;
};

} // namespace dune3d
