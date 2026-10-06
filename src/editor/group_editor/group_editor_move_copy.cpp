#include "group_editor_move_copy.hpp"
#include "document/group/group_move_copy.hpp"
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

GroupEditorMoveCopy::GroupEditorMoveCopy(Core &core, const UUID &group_uu) : GroupEditor(core, group_uu)
{
    auto &group = get_group();

    m_tx_sp = make_spinbutton(-1e4, 1e4, group.m_translation.x);
    m_ty_sp = make_spinbutton(-1e4, 1e4, group.m_translation.y);
    m_tz_sp = make_spinbutton(-1e4, 1e4, group.m_translation.z);
    connect_spinbutton(*m_tx_sp, sigc::mem_fun(*this, &GroupEditorMoveCopy::update));
    connect_spinbutton(*m_ty_sp, sigc::mem_fun(*this, &GroupEditorMoveCopy::update));
    connect_spinbutton(*m_tz_sp, sigc::mem_fun(*this, &GroupEditorMoveCopy::update));
    {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 3);
        box->append(*m_tx_sp);
        box->append(*m_ty_sp);
        box->append(*m_tz_sp);
        grid_attach_label_and_widget(*this, "Translation (x/y/z)", *box, m_top);
    }

    m_ax_sp = make_spinbutton(-1, 1, group.m_rotation_axis.x);
    m_ay_sp = make_spinbutton(-1, 1, group.m_rotation_axis.y);
    m_az_sp = make_spinbutton(-1, 1, group.m_rotation_axis.z);
    connect_spinbutton(*m_ax_sp, sigc::mem_fun(*this, &GroupEditorMoveCopy::update));
    connect_spinbutton(*m_ay_sp, sigc::mem_fun(*this, &GroupEditorMoveCopy::update));
    connect_spinbutton(*m_az_sp, sigc::mem_fun(*this, &GroupEditorMoveCopy::update));
    {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 3);
        box->append(*m_ax_sp);
        box->append(*m_ay_sp);
        box->append(*m_az_sp);
        grid_attach_label_and_widget(*this, "Rotation axis (x/y/z)", *box, m_top);
    }

    m_angle_sp = make_spinbutton(-360, 360, group.m_rotation_angle);
    m_angle_sp->set_increments(1, 1);
    m_angle_sp->set_digits(2);
    connect_spinbutton(*m_angle_sp, sigc::mem_fun(*this, &GroupEditorMoveCopy::update));
    grid_attach_label_and_widget(*this, "Rotation angle", *m_angle_sp, m_top);

    m_copy_cb = Gtk::make_managed<Gtk::CheckButton>();
    m_copy_cb->set_active(group.m_copy);
    m_copy_cb->signal_toggled().connect([this] {
        if (is_reloading())
            return;
        get_group().m_copy = m_copy_cb->get_active();
        m_core.get_current_document().set_group_update_solid_model_pending(get_group().m_uuid);
        m_signal_changed.emit(CommitMode::IMMEDIATE);
    });
    grid_attach_label_and_widget(*this, "Copy (not yet implemented)", *m_copy_cb, m_top);
}

void GroupEditorMoveCopy::do_reload()
{
    GroupEditor::do_reload();
    auto &group = get_group();
    m_tx_sp->set_value(group.m_translation.x);
    m_ty_sp->set_value(group.m_translation.y);
    m_tz_sp->set_value(group.m_translation.z);
    m_ax_sp->set_value(group.m_rotation_axis.x);
    m_ay_sp->set_value(group.m_rotation_axis.y);
    m_az_sp->set_value(group.m_rotation_axis.z);
    m_angle_sp->set_value(group.m_rotation_angle);
    m_copy_cb->set_active(group.m_copy);
}

GroupMoveCopy &GroupEditorMoveCopy::get_group()
{
    return m_core.get_current_document().get_group<GroupMoveCopy>(m_group_uu);
}

bool GroupEditorMoveCopy::update()
{
    if (is_reloading())
        return false;
    auto &group = get_group();
    const glm::dvec3 new_translation{m_tx_sp->get_value(), m_ty_sp->get_value(), m_tz_sp->get_value()};
    const glm::dvec3 new_axis{m_ax_sp->get_value(), m_ay_sp->get_value(), m_az_sp->get_value()};
    const double new_angle = m_angle_sp->get_value();
    if (group.m_translation == new_translation && group.m_rotation_axis == new_axis
        && group.m_rotation_angle == new_angle)
        return false;
    group.m_translation = new_translation;
    group.m_rotation_axis = new_axis;
    group.m_rotation_angle = new_angle;
    m_core.get_current_document().set_group_update_solid_model_pending(group.m_uuid);
    return true;
}

} // namespace dune3d
