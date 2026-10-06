#pragma once
#include "group_editor_face_operation.hpp"

namespace dune3d {

class GroupSilhouetteSplit;

class GroupEditorSilhouetteSplit : public GroupEditorFaceOperation {
public:
    GroupEditorSilhouetteSplit(Core &core, const UUID &group_uu);

    void do_reload() override;

protected:
    void construct_extra() override;

private:
    GroupSilhouetteSplit &get_group_silhouette_split();

    bool update();

    Gtk::SpinButton *m_dx_sp = nullptr;
    Gtk::SpinButton *m_dy_sp = nullptr;
    Gtk::SpinButton *m_dz_sp = nullptr;
};

} // namespace dune3d
