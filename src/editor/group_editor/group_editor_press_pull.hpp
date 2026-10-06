#pragma once
#include "group_editor_face_operation.hpp"

namespace dune3d {

class GroupPressPull;

class GroupEditorPressPull : public GroupEditorFaceOperation {
public:
    GroupEditorPressPull(Core &core, const UUID &group_uu);

    void do_reload() override;

protected:
    void construct_extra() override;

private:
    GroupPressPull &get_group_press_pull();

    bool update_offset();

    Gtk::SpinButton *m_offset_sp = nullptr;
};

} // namespace dune3d
