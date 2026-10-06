#include "group_editor_split_face.hpp"
#include "document/group/group_split_face.hpp"
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

GroupEditorSplitFace::GroupEditorSplitFace(Core &core, const UUID &group_uu) : GroupEditorFaceOperation(core, group_uu)
{
    construct();
}

void GroupEditorSplitFace::construct_extra()
{
    auto &group = get_group_split_face();

    m_px_sp = make_spinbutton(-1e4, 1e4, group.m_plane_point.x);
    m_py_sp = make_spinbutton(-1e4, 1e4, group.m_plane_point.y);
    m_pz_sp = make_spinbutton(-1e4, 1e4, group.m_plane_point.z);
    connect_spinbutton(*m_px_sp, sigc::mem_fun(*this, &GroupEditorSplitFace::update));
    connect_spinbutton(*m_py_sp, sigc::mem_fun(*this, &GroupEditorSplitFace::update));
    connect_spinbutton(*m_pz_sp, sigc::mem_fun(*this, &GroupEditorSplitFace::update));
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
    connect_spinbutton(*m_nx_sp, sigc::mem_fun(*this, &GroupEditorSplitFace::update));
    connect_spinbutton(*m_ny_sp, sigc::mem_fun(*this, &GroupEditorSplitFace::update));
    connect_spinbutton(*m_nz_sp, sigc::mem_fun(*this, &GroupEditorSplitFace::update));
    {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 3);
        box->append(*m_nx_sp);
        box->append(*m_ny_sp);
        box->append(*m_nz_sp);
        grid_attach_label_and_widget(*this, "Plane normal (x/y/z)", *box, m_top);
    }
}

void GroupEditorSplitFace::do_reload()
{
    GroupEditorFaceOperation::do_reload();
    auto &group = get_group_split_face();
    m_px_sp->set_value(group.m_plane_point.x);
    m_py_sp->set_value(group.m_plane_point.y);
    m_pz_sp->set_value(group.m_plane_point.z);
    m_nx_sp->set_value(group.m_plane_normal.x);
    m_ny_sp->set_value(group.m_plane_normal.y);
    m_nz_sp->set_value(group.m_plane_normal.z);
}

GroupSplitFace &GroupEditorSplitFace::get_group_split_face()
{
    return m_core.get_current_document().get_group<GroupSplitFace>(m_group_uu);
}

bool GroupEditorSplitFace::update()
{
    if (is_reloading())
        return false;
    auto &group = get_group_split_face();
    const glm::dvec3 new_point{m_px_sp->get_value(), m_py_sp->get_value(), m_pz_sp->get_value()};
    const glm::dvec3 new_normal{m_nx_sp->get_value(), m_ny_sp->get_value(), m_nz_sp->get_value()};
    if (group.m_plane_point == new_point && group.m_plane_normal == new_normal)
        return false;
    group.m_plane_point = new_point;
    group.m_plane_normal = new_normal;
    m_core.get_current_document().set_group_update_solid_model_pending(group.m_uuid);
    return true;
}

} // namespace dune3d
