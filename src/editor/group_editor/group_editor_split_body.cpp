#include "group_editor_split_body.hpp"
#include "document/group/group_split_body.hpp"
#include "util/gtk_util.hpp"
#include "core/core.hpp"

namespace dune3d {

static Gtk::SpinButton *make_spinbutton(double lo, double hi, double value)
{
    auto sp = Gtk::make_managed<Gtk::SpinButton>();
    sp->set_hexpand(true);
    sp->set_range(lo, hi);
    sp->set_increments(.1, .1);
    sp->set_digits(4);
    sp->set_value(value);
    return sp;
}

GroupEditorSplitBody::GroupEditorSplitBody(Core &core, const UUID &group_uu) : GroupEditor(core, group_uu)
{
    auto &group = get_group();

    m_px_sp = make_spinbutton(-1e4, 1e4, group.m_plane_point.x);
    m_py_sp = make_spinbutton(-1e4, 1e4, group.m_plane_point.y);
    m_pz_sp = make_spinbutton(-1e4, 1e4, group.m_plane_point.z);
    connect_spinbutton(*m_px_sp, sigc::mem_fun(*this, &GroupEditorSplitBody::update));
    connect_spinbutton(*m_py_sp, sigc::mem_fun(*this, &GroupEditorSplitBody::update));
    connect_spinbutton(*m_pz_sp, sigc::mem_fun(*this, &GroupEditorSplitBody::update));
    {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 3);
        box->append(*m_px_sp);
        box->append(*m_py_sp);
        box->append(*m_pz_sp);
        grid_attach_label_and_widget(*this, "Plane point (x/y/z)", *box, m_top);
    }

    m_nx_sp = make_spinbutton(-1, 1, group.m_plane_normal.x);
    m_ny_sp = make_spinbutton(-1, 1, group.m_plane_normal.y);
    m_nz_sp = make_spinbutton(-1, 1, group.m_plane_normal.z);
    connect_spinbutton(*m_nx_sp, sigc::mem_fun(*this, &GroupEditorSplitBody::update));
    connect_spinbutton(*m_ny_sp, sigc::mem_fun(*this, &GroupEditorSplitBody::update));
    connect_spinbutton(*m_nz_sp, sigc::mem_fun(*this, &GroupEditorSplitBody::update));
    {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 3);
        box->append(*m_nx_sp);
        box->append(*m_ny_sp);
        box->append(*m_nz_sp);
        grid_attach_label_and_widget(*this, "Plane normal, kept side (x/y/z)", *box, m_top);
    }
}

void GroupEditorSplitBody::do_reload()
{
    GroupEditor::do_reload();
    auto &group = get_group();
    m_px_sp->set_value(group.m_plane_point.x);
    m_py_sp->set_value(group.m_plane_point.y);
    m_pz_sp->set_value(group.m_plane_point.z);
    m_nx_sp->set_value(group.m_plane_normal.x);
    m_ny_sp->set_value(group.m_plane_normal.y);
    m_nz_sp->set_value(group.m_plane_normal.z);
}

GroupSplitBody &GroupEditorSplitBody::get_group()
{
    return m_core.get_current_document().get_group<GroupSplitBody>(m_group_uu);
}

bool GroupEditorSplitBody::update()
{
    if (is_reloading())
        return false;
    auto &group = get_group();
    const glm::dvec3 new_point{m_px_sp->get_value(), m_py_sp->get_value(), m_pz_sp->get_value()};
    const glm::dvec3 new_normal{m_nx_sp->get_value(), m_ny_sp->get_value(), m_nz_sp->get_value()};
    if (group.m_plane_point == new_point && group.m_plane_normal == new_normal)
        return false;
    group.m_plane_point = new_point;
    group.m_plane_normal = new_normal;
    auto &doc = m_core.get_current_document();
    doc.set_group_update_solid_model_pending(group.m_uuid);
    if (group.m_result_group)
        doc.set_group_update_solid_model_pending(group.m_result_group);
    return true;
}

} // namespace dune3d
