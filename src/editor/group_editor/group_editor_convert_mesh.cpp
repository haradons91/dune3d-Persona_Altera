#include "group_editor_convert_mesh.hpp"
#include "document/group/group_convert_mesh.hpp"
#include "widgets/spin_button_dim.hpp"
#include "util/gtk_util.hpp"
#include "core/core.hpp"

namespace dune3d {

GroupEditorConvertMesh::GroupEditorConvertMesh(Core &core, const UUID &group_uu) : GroupEditor(core, group_uu)
{
    auto &group = get_group();

    auto items = Gtk::StringList::create();
    items->append("Direct");
    items->append("Weld + Sew");
    items->append("Decimate + Sew");
    m_algorithm_combo = Gtk::make_managed<Gtk::DropDown>(items);
    m_algorithm_combo->set_selected(static_cast<guint>(group.m_algorithm));
    m_algorithm_combo->property_selected().signal_changed().connect([this] {
        if (is_reloading())
            return;
        auto &group = get_group();
        group.m_algorithm = static_cast<GroupConvertMesh::Algorithm>(m_algorithm_combo->get_selected());
        update_param_sensitivity();
        m_core.get_current_document().set_group_update_solid_model_pending(group.m_uuid);
        m_signal_changed.emit(CommitMode::IMMEDIATE);
    });
    grid_attach_label_and_widget(*this, "Algorithm", *m_algorithm_combo, m_top);

    m_decimate_target_sp = Gtk::make_managed<Gtk::SpinButton>();
    m_decimate_target_sp->set_range(100, 20000);
    m_decimate_target_sp->set_increments(100, 1000);
    m_decimate_target_sp->set_value(group.m_decimate_target_faces);
    grid_attach_label_and_widget(*this, "Decimation target", *m_decimate_target_sp, m_top);
    connect_spinbutton(*m_decimate_target_sp, [this] {
        if (is_reloading())
            return false;
        auto &group = get_group();
        const auto value = (unsigned int)m_decimate_target_sp->get_value_as_int();
        if (group.m_decimate_target_faces == value)
            return false;
        group.m_decimate_target_faces = value;
        m_core.get_current_document().set_group_update_solid_model_pending(group.m_uuid);
        return true;
    });

    m_weld_tolerance_sp = Gtk::make_managed<SpinButtonDim>();
    m_weld_tolerance_sp->set_range(0.0001, 10);
    m_weld_tolerance_sp->set_decimal_places(4);
    m_weld_tolerance_sp->set_value(group.m_weld_tolerance);
    grid_attach_label_and_widget(*this, "Weld tolerance", *m_weld_tolerance_sp, m_top);
    connect_spinbutton(*m_weld_tolerance_sp, [this] {
        if (is_reloading())
            return false;
        auto &group = get_group();
        const auto value = m_weld_tolerance_sp->get_value();
        if (group.m_weld_tolerance == value)
            return false;
        group.m_weld_tolerance = value;
        m_core.get_current_document().set_group_update_solid_model_pending(group.m_uuid);
        return true;
    });

    update_param_sensitivity();
}

void GroupEditorConvertMesh::update_param_sensitivity()
{
    const auto algo = get_group().m_algorithm;
    m_decimate_target_sp->set_sensitive(algo == GroupConvertMesh::Algorithm::DECIMATE_SEW);
    m_weld_tolerance_sp->set_sensitive(algo == GroupConvertMesh::Algorithm::WELD_SEW);
}

void GroupEditorConvertMesh::do_reload()
{
    GroupEditor::do_reload();
    auto &group = get_group();
    m_algorithm_combo->set_selected(static_cast<guint>(group.m_algorithm));
    m_decimate_target_sp->set_value(group.m_decimate_target_faces);
    m_weld_tolerance_sp->set_value(group.m_weld_tolerance);
    update_param_sensitivity();
}

GroupConvertMesh &GroupEditorConvertMesh::get_group()
{
    return m_core.get_current_document().get_group<GroupConvertMesh>(m_group_uu);
}

} // namespace dune3d
