#include "group_editor_silhouette_split.hpp"
#include "document/group/group_silhouette_split.hpp"
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

GroupEditorSilhouetteSplit::GroupEditorSilhouetteSplit(Core &core, const UUID &group_uu)
    : GroupEditorFaceOperation(core, group_uu)
{
    construct();
}

void GroupEditorSilhouetteSplit::construct_extra()
{
    auto &group = get_group_silhouette_split();

    m_dx_sp = make_spinbutton(-1, 1, group.m_direction.x);
    m_dy_sp = make_spinbutton(-1, 1, group.m_direction.y);
    m_dz_sp = make_spinbutton(-1, 1, group.m_direction.z);
    connect_spinbutton(*m_dx_sp, sigc::mem_fun(*this, &GroupEditorSilhouetteSplit::update));
    connect_spinbutton(*m_dy_sp, sigc::mem_fun(*this, &GroupEditorSilhouetteSplit::update));
    connect_spinbutton(*m_dz_sp, sigc::mem_fun(*this, &GroupEditorSilhouetteSplit::update));
    {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 3);
        box->append(*m_dx_sp);
        box->append(*m_dy_sp);
        box->append(*m_dz_sp);
        grid_attach_label_and_widget(*this, "Silhouette direction (x/y/z)", *box, m_top);
    }
}

void GroupEditorSilhouetteSplit::do_reload()
{
    GroupEditorFaceOperation::do_reload();
    auto &group = get_group_silhouette_split();
    m_dx_sp->set_value(group.m_direction.x);
    m_dy_sp->set_value(group.m_direction.y);
    m_dz_sp->set_value(group.m_direction.z);
}

GroupSilhouetteSplit &GroupEditorSilhouetteSplit::get_group_silhouette_split()
{
    return m_core.get_current_document().get_group<GroupSilhouetteSplit>(m_group_uu);
}

bool GroupEditorSilhouetteSplit::update()
{
    if (is_reloading())
        return false;
    auto &group = get_group_silhouette_split();
    const glm::dvec3 new_direction{m_dx_sp->get_value(), m_dy_sp->get_value(), m_dz_sp->get_value()};
    if (group.m_direction == new_direction)
        return false;
    group.m_direction = new_direction;
    m_core.get_current_document().set_group_update_solid_model_pending(group.m_uuid);
    return true;
}

} // namespace dune3d
