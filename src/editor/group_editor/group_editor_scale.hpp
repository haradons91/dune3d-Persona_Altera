#pragma once
#include "group_editor.hpp"

namespace dune3d {

class GroupScale;

class GroupEditorScale : public GroupEditor {
public:
    GroupEditorScale(Core &core, const UUID &group_uu);

    void do_reload() override;

private:
    GroupScale &get_group();

    bool update_factor();

    Gtk::SpinButton *m_factor_sp = nullptr;
};

} // namespace dune3d
