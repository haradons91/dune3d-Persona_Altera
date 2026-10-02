#include "group_editor_scale.hpp"
#include "document/group/group_scale.hpp"
#include "util/gtk_util.hpp"
#include "core/core.hpp"

namespace dune3d {

GroupEditorScale::GroupEditorScale(Core &core, const UUID &group_uu) : GroupEditor(core, group_uu)
{
    auto &group = get_group();

    m_factor_sp = Gtk::make_managed<Gtk::SpinButton>();
    m_factor_sp->set_hexpand(true);
    m_factor_sp->set_range(1e-3, 1e3);
    m_factor_sp->set_increments(.1, .1);
    m_factor_sp->set_digits(4);
    m_factor_sp->set_value(group.m_factor);
    connect_spinbutton(*m_factor_sp, sigc::mem_fun(*this, &GroupEditorScale::update_factor));
    grid_attach_label_and_widget(*this, "Factor", *m_factor_sp, m_top);
}

void GroupEditorScale::do_reload()
{
    GroupEditor::do_reload();
    m_factor_sp->set_value(get_group().m_factor);
}

GroupScale &GroupEditorScale::get_group()
{
    return m_core.get_current_document().get_group<GroupScale>(m_group_uu);
}

bool GroupEditorScale::update_factor()
{
    if (is_reloading())
        return false;
    if (get_group().m_factor == m_factor_sp->get_value())
        return false;
    get_group().m_factor = m_factor_sp->get_value();
    m_core.get_current_document().set_group_update_solid_model_pending(get_group().m_uuid);
    return true;
}

} // namespace dune3d
