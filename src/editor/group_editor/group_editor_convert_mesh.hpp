#pragma once
#include "group_editor.hpp"

namespace dune3d {

class GroupConvertMesh;
class SpinButtonDim;

class GroupEditorConvertMesh : public GroupEditor {
public:
    GroupEditorConvertMesh(Core &core, const UUID &group_uu);

    void do_reload() override;

private:
    GroupConvertMesh &get_group();

    Gtk::DropDown *m_algorithm_combo = nullptr;
    Gtk::SpinButton *m_decimate_target_sp = nullptr;
    SpinButtonDim *m_weld_tolerance_sp = nullptr;

    void update_param_sensitivity();
};

} // namespace dune3d
