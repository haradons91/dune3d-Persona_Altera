#pragma once
#include "group_editor_face_operation.hpp"

namespace dune3d {

class GroupReplaceFace;

class GroupEditorReplaceFace : public GroupEditorFaceOperation {
public:
    GroupEditorReplaceFace(Core &core, const UUID &group_uu);

    void do_reload() override;

protected:
    void construct_extra() override;

private:
    GroupReplaceFace &get_group_replace_face();

    void update_label();

    Gtk::Label *m_reference_label = nullptr;
};

} // namespace dune3d
