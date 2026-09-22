#include "editor.hpp"
#include "workspace_browser.hpp"
#include "dune3d_appwindow.hpp"
#include "widgets/constraints_box.hpp"
#include "document/group/all_groups.hpp"
#include "widgets/sketch_plane_selector.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/entity/entity_circle2d.hpp"
#include "document/entity/entity_step.hpp"
#include "document/constraint/constraint_lock_rotation.hpp"
#include "util/selection_util.hpp"
#include "canvas/canvas.hpp"
#include "workspace_browser.hpp"
#include "util/template_util.hpp"
#include "util/gtk_util.hpp"
#include "core/tool_id.hpp"
#include "nlohmann/json.hpp"
#include "action/action_id.hpp"
#include "document/solid_model/solid_model.hpp"
#include "document/group/igroup_solid_model.hpp"
#include "widgets/select_groups_dialog.hpp"
#include "core/tool_data_create_circular_sweep_group.hpp"
#include "util/glm_util.hpp"
#include "util/paths.hpp"
#include "util/debug.hpp"
#include <format>

namespace dune3d {
using json = nlohmann::json;

constexpr float default_sketch_camera_distance = 200.0f;

void Editor::init_workspace_browser()
{
    m_workspace_browser_stack = Gtk::make_managed<Gtk::Stack>();
    m_workspace_browser_stack->set_vexpand(true);
    m_win.get_left_bar().set_start_child(*m_workspace_browser_stack);

    // Connected once here (rather than per-document in connect_workspace_browser)
    // since it always refreshes whichever browser is currently active, not the
    // browser that was current when it was connected.
    m_core.signal_rebuilt().connect([this] {
        Glib::signal_idle().connect_once([this] {
            if (!m_current_workspace_view || !m_workspace_browser)
                return;
            m_workspace_browser->update_documents(m_workspace_views.at(m_current_workspace_view).m_documents);
        });
    });
}

void Editor::connect_workspace_browser(WorkspaceBrowser &browser)
{
    m_workspace_browser = &browser;
    m_workspace_browser->signal_close_document().connect([this](const UUID &doc_uu) {
        get_canvas().grab_focus();
        close_document(doc_uu, nullptr, nullptr);
    });

    m_workspace_browser->signal_group_selected().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_group_selected));
    m_workspace_browser->signal_group_activated().connect([this](const UUID &uu_doc, const UUID &uu_group) {
        if (uu_doc != m_core.get_current_idocument_info().get_uuid())
            return;
        const auto type = m_core.get_current_document().get_group(uu_group).get_type();
        if (type == Group::Type::SKETCH) {
            m_extrude_editing = false;
            auto &doc_view = get_current_document_view();
            if (!m_sketch_editing) {
                m_sketch_plane_previous_cam_quat = get_canvas().get_cam_quat();
                m_sketch_plane_previous_cam_distance = get_canvas().get_cam_distance();
                m_sketch_previous_visibility = doc_view.m_group_views[uu_group].m_visible;
            }
            // A hidden sketch must be visible while it is being edited, but
            // its original visibility is restored when editing ends.
            doc_view.m_group_views[uu_group].m_visible = true;
            m_sketch_editing = true;
            auto &sketch = m_core.get_current_document().get_group(uu_group);
            if (sketch.m_active_wrkpl) {
                auto &workplane = m_core.get_current_document().get_entity<EntityWorkplane>(sketch.m_active_wrkpl);
                workplane.m_visible = true;
                auto camera_quat = workplane.m_normal;
                const auto &reference = m_core.get_current_document().get_reference_group();
                if (sketch.m_active_wrkpl == reference.get_workplane_zx_uuid()) {
                    // XZ has +Y as its positive normal, but open the sketch
                    // from the Front (-Y) side so X is right and Z is up.
                    camera_quat = glm::quatLookAt(glm::dvec3(0, 1, 0), glm::dvec3(0, 0, 1));
                }
                get_canvas().set_cam_distance(default_sketch_camera_distance, Canvas::ZoomCenter::SCREEN);
                get_canvas().animate_to_cam_quat(glm::quat(camera_quat));
            }
        }
        else if (type == Group::Type::EXTRUDE) {
            m_sketch_editing = false;
            m_extrude_editing = true;
        }
        else {
            return;
        }
        update_sketch_mode_ui();
        canvas_update();
        Glib::signal_idle().connect_once([this] {
            if (m_sketch_editing)
                get_canvas().set_cam_distance(default_sketch_camera_distance, Canvas::ZoomCenter::SCREEN);
        });
    });
    m_workspace_browser->signal_add_group().connect(sigc::mem_fun(*this, &Editor::on_add_group));
    m_workspace_browser->signal_delete_current_group().connect(sigc::mem_fun(*this, &Editor::on_delete_current_group));
    m_workspace_browser->signal_move_group().connect(sigc::mem_fun(*this, &Editor::on_move_group));
    m_workspace_browser->signal_document_checked().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_document_checked));
    m_workspace_browser->signal_origin_checked().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_origin_checked));
    m_workspace_browser->signal_sketches_checked().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_sketches_checked));
    m_workspace_browser->signal_group_checked().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_group_checked));
    m_workspace_browser->signal_body_checked().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_body_checked));
    m_workspace_browser->signal_body_solid_model_checked().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_body_solid_model_checked));
    m_workspace_browser->signale_active_link().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_activate_link));
    m_workspace_browser->signal_rename_body().connect(sigc::mem_fun(*this, &Editor::on_workspace_browser_rename_body));
    m_workspace_browser->signal_reset_body_color().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_reset_body_color));
    m_workspace_browser->signal_set_body_color().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_set_body_color));
    m_workspace_browser->signal_export_body_stl().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_export_body_stl));
    m_workspace_browser->signal_export_body_step().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_export_body_step));
    m_workspace_browser->signal_body_expanded().connect([this](const UUID &body_uu, bool expanded) {
        if (m_core.get_current_document()
                            .get_group(m_core.get_current_group())
                            .find_body(m_core.get_current_document())
                            .group.m_uuid
                    == body_uu
            && expanded == false) {
            return false;
        }
        get_current_document_view().m_body_views[body_uu].m_expanded = expanded;
        return true;
    });

    m_workspace_browser->set_sensitive(m_core.has_documents());
}

void Editor::ensure_workspace_browser(const UUID &doc_uuid)
{
    if (m_workspace_browsers.contains(doc_uuid))
        return;

    auto browser = Gtk::make_managed<WorkspaceBrowser>(m_core, doc_uuid);
    connect_workspace_browser(*browser);
    m_workspace_browser_stack->add(*browser, static_cast<std::string>(doc_uuid));
    m_workspace_browsers.emplace(doc_uuid, browser);
}

void Editor::show_workspace_browser(const UUID &doc_uuid)
{
    ensure_workspace_browser(doc_uuid);
    m_workspace_browser = m_workspace_browsers.at(doc_uuid);
    m_workspace_browser_stack->set_visible_child(*m_workspace_browser);
    m_workspace_browser->set_sensitive(true);
    if (m_workspace_views.contains(m_current_workspace_view))
        m_workspace_browser->update_documents(get_current_document_views());
}

void Editor::remove_workspace_browser(const UUID &doc_uuid)
{
    auto it = m_workspace_browsers.find(doc_uuid);
    if (it == m_workspace_browsers.end())
        return;
    const bool was_active = (m_workspace_browser == it->second);
    m_workspace_browser_stack->remove(*it->second);
    m_workspace_browsers.erase(it);
    if (was_active) {
        m_workspace_browser = nullptr;
        if (m_core.has_documents())
            show_workspace_browser(m_core.get_current_idocument_info().get_uuid());
    }
}

void Editor::reset_sketch_editing_state()
{
    if (m_selecting_sketch_plane) {
        m_win.get_sketch_plane_selector().set_visible(false);
        get_canvas().set_selection_mode(SelectionMode::NORMAL);
    }
    m_selecting_sketch_plane = false;
    m_sketch_editing = false;
    m_extrude_editing = false;
    m_extrude_dragging = false;
    m_extrude_drag_changed = false;
    m_sketch_previous_visibility.reset();
    m_sketch_plane_previous_cam_quat.reset();
    m_sketch_plane_previous_cam_distance.reset();
    m_sketch_plane_grid.reset();
    m_restore_sketch_plane_cam_on_undo = false;
    m_sketch_plane_created_group.reset();
    m_sketch_redo_reenter_group.reset();
    m_sketch_grid_offset.reset();
    m_sketch_finished_for_undo.reset();
    m_sketch_entered_by_undo = false;
    m_sketch_finished_return_cam_distance.reset();
    update_sketch_mode_ui();
}

void Editor::on_workspace_browser_group_selected(const UUID &uu_doc, const UUID &uu_group)
{
    if (m_core.tool_is_active())
        return;
    m_sketch_editing = false;
    auto &idoc = m_core.get_current_idocument_info();
    if (idoc.get_uuid() == uu_doc && idoc.get_current_group() == uu_group)
        return;
    if (idoc.get_uuid() != uu_doc)
        reset_sketch_editing_state();
    m_core.set_current_document(uu_doc);
    m_workspace_views.at(m_current_workspace_view).m_current_document = uu_doc;
    update_version_info();
    get_current_document_view().m_current_group = uu_group;
    set_current_group(uu_group);
    update_timeline();
}

void Editor::on_add_group(Group::Type group_type, WorkspaceBrowserAddGroupMode add_group_mode)
{
    DUNE3D_TRACE(DebugCategory::EXTRUDE);
    if (m_core.tool_is_active())
        return;
    auto &doc = m_core.get_current_document();
    auto &current_group = doc.get_group(m_core.get_current_group());
    Group *new_group = nullptr;
    static const std::string toast_prefix = "Couldn't create group\n";
    if (group_type == Group::Type::SKETCH) {
        // Keep the current view unchanged while the user chooses a plane.
        m_selecting_sketch_plane = true;
        m_sketch_plane_grid.reset();
        m_sketch_grid_offset.reset();
        m_sketch_plane_previous_cam_quat = get_canvas().get_cam_quat();
        m_sketch_plane_previous_cam_distance = get_canvas().get_cam_distance();
        m_sketch_finished_return_cam_distance.reset();
        debug_log(DebugCategory::UI,
                  std::format("sketch camera start distance={:.6f}", *m_sketch_plane_previous_cam_distance));
        m_restore_sketch_plane_cam_on_undo = false;
        m_sketch_plane_created_group.reset();
        m_sketch_plane_current_group = current_group.m_uuid;
        m_sketch_plane_add_group_mode = add_group_mode;
        get_canvas().grab_focus();

        std::optional<SelectableRef> face_selection;
        for (const auto &selection : get_canvas().get_selection()) {
            if (selection.type == SelectableRef::Type::SOLID_MODEL_FACE) {
                face_selection = selection;
                break;
            }
        }
        if (!face_selection) {
            if (auto hover = get_canvas().get_hover_selection(); hover
                && hover->type == SelectableRef::Type::SOLID_MODEL_FACE)
                face_selection = hover;
        }
        if (face_selection) {
            get_canvas().set_selection({}, false);
            finish_sketch_face_selection(face_selection->item, face_selection->point);
            return;
        }

        get_canvas().set_selection({}, false);
        get_canvas().set_selection_mode(SelectionMode::HOVER_ONLY);
        m_win.get_sketch_plane_selector().set_visible(false);
        canvas_update();
        m_workspace_browser->show_toast("Select a reference plane for the sketch");
        return;
    }
    else if (group_type == Group::Type::EXTRUDE) {
        if (!current_group.m_active_wrkpl) {
            m_workspace_browser->show_toast(toast_prefix + "Current group needs an active workplane");
            return;
        }
        auto &group = doc.insert_group<GroupExtrude>(UUID::random(), current_group.m_uuid);
        new_group = &group;
        group.m_wrkpl = current_group.m_active_wrkpl;
        group.m_dvec = doc.get_entity<EntityWorkplane>(group.m_wrkpl).get_normal_vector();
        group.m_source_group = current_group.m_uuid;
        // A sketch attached directly to a solid face is normally used for a
        // pocket/cut.  Start in Difference so entering a depth through the
        // extrusion editor or its textbox cuts the supporting body.  The
        // extrusion drag logic changes this back to Union when the handle is
        // moved away from the body.
        if (const auto *sketch = dynamic_cast<const GroupSketch *>(&current_group);
            sketch && sketch->m_attached_to_face)
            group.m_operation = IGroupSolidModel::Operation::DIFFERENCE;
        bool have_profile_selection = false;
        std::set<unsigned int> selected_profiles;
        for (const auto &selection : get_canvas().get_selection()) {
            debug_log(DebugCategory::EXTRUDE,
                      std::format("extrude selection type={} item={} point={}", static_cast<int>(selection.type),
                                  static_cast<std::string>(selection.item), selection.point));
            if (selection.type == SelectableRef::Type::SKETCH_PROFILE
                && selection.item == current_group.m_uuid) {
                selected_profiles.insert(selection.point);
                have_profile_selection = true;
            }
        }
        group.m_source_profiles = selected_profiles;
        // Region selections stay as cells.  Do not expand them into boundary
        // paths: doing so makes the outer region and its nested holes appear
        // selected together in the extrusion preview.
        // If the profile face is occluded by sketch geometry, the canvas may
        // return the selected sketch edges instead.  Convert those edges back
        // to profile indices so Extrude still uses only the intended loops.
        if (!have_profile_selection) {
            std::set<UUID> selected_entities;
            for (const auto &selection : get_canvas().get_selection()) {
                if (selection.type == SelectableRef::Type::ENTITY)
                    selected_entities.insert(selection.item);
            }
            if (!selected_entities.empty()) {
                const auto paths = paths::Paths::from_document(doc, group.m_wrkpl, group.m_source_group);
                for (size_t profile_idx = 0; profile_idx < paths.paths.size(); profile_idx++) {
                    if (std::ranges::any_of(paths.paths.at(profile_idx), [&selected_entities](const auto &entry) {
                            return selected_entities.contains(entry.second.entity.m_uuid);
                        }))
                        group.m_source_paths.insert(profile_idx);
                }
            }
        }
        if (debug_enabled(DebugCategory::EXTRUDE)) {
            std::string paths_str;
            for (const auto path : group.m_source_paths)
                paths_str += std::to_string(path) + ',';
            debug_log(DebugCategory::EXTRUDE,
                      "extrude source_paths=" + paths_str
                              + std::format(" have_profile_selection={}", have_profile_selection));
        }
    }
    else if (group_type == Group::Type::REVOLVE) {
        if (!current_group.m_active_wrkpl) {
            m_workspace_browser->show_toast(toast_prefix + "Current group needs an active workplane");
            return;
        }
        tool_begin(ToolID::CREATE_REVOLVE_GROUP, std::make_unique<ToolDataCreateCircularSweepGroup>(
                                                         add_group_mode == WorkspaceBrowserAddGroupMode::WITH_BODY));
        return;
    }
    else if (group_type == Group::Type::LATHE) {
        if (!current_group.m_active_wrkpl) {
            m_workspace_browser->show_toast(toast_prefix + "Current group needs an active workplane");
            return;
        }
        tool_begin(ToolID::CREATE_LATHE_GROUP, std::make_unique<ToolDataCreateCircularSweepGroup>(
                                                       add_group_mode == WorkspaceBrowserAddGroupMode::WITH_BODY));
        return;
    }
    else if (any_of(group_type, Group::Type::FILLET, Group::Type::CHAMFER)) {
        auto solid_model = SolidModel::get_last_solid_model(doc, current_group, SolidModel::IncludeGroup::YES);
        if (!solid_model) {
            m_workspace_browser->show_toast(toast_prefix + "Body has no solid model");
            return;
        }
        if (group_type == Group::Type::FILLET) {
            auto &group = doc.insert_group<GroupFillet>(UUID::random(), current_group.m_uuid);
            new_group = &group;
        }
        else {
            auto &group = doc.insert_group<GroupChamfer>(UUID::random(), current_group.m_uuid);
            new_group = &group;
        }
    }
    else if (group_type == Group::Type::LINEAR_ARRAY) {
        auto &group = doc.insert_group<GroupLinearArray>(UUID::random(), current_group.m_uuid);
        new_group = &group;
        group.m_active_wrkpl = current_group.m_active_wrkpl;
        group.m_source_group = current_group.m_uuid;
    }
    else if (any_of(group_type, Group::Type::POLAR_ARRAY, Group::Type::MIRROR_HORIZONTAL,
                    Group::Type::MIRROR_VERTICAL)) {
        UUID wrkpl;
        if (current_group.m_active_wrkpl) {
            wrkpl = current_group.m_active_wrkpl;
        }
        else {
            auto sel = get_canvas().get_selection();
            auto owrkpl = point_from_selection(doc, sel, Entity::Type::WORKPLANE);
            if (owrkpl)
                wrkpl = owrkpl->entity;
        }
        if (!wrkpl) {
            m_workspace_browser->show_toast(toast_prefix + "Current group needs an active workplane or select one");
            return;
        }
        GroupReplicate *group = nullptr;
        if (group_type == GroupType::POLAR_ARRAY)
            group = &doc.insert_group<GroupPolarArray>(UUID::random(), current_group.m_uuid);
        else if (group_type == GroupType::MIRROR_HORIZONTAL)
            group = &doc.insert_group<GroupMirrorHorizontal>(UUID::random(), current_group.m_uuid);
        else if (group_type == GroupType::MIRROR_VERTICAL)
            group = &doc.insert_group<GroupMirrorVertical>(UUID::random(), current_group.m_uuid);
        new_group = group;
        group->m_active_wrkpl = wrkpl;
        group->m_source_group = current_group.m_uuid;
    }
    else if (group_type == Group::Type::LOFT) {
        auto dia = SelectGroupsDialog::create(m_core.get_current_document(), m_core.get_current_group(), {});
        dia->set_transient_for(m_win);
        dia->present();
        dia->signal_changed().connect([this, dia, &current_group, &doc] {
            auto groups = dia->get_selected_groups();
            if (groups.size() < 2) {
                m_workspace_browser->show_toast(toast_prefix + "Select at least two groups");
                return;
            }
            auto &group = doc.insert_group<GroupLoft>(UUID::random(), current_group.m_uuid);
            for (const auto &uu : groups) {
                const auto &src = doc.get_group(uu);
                group.m_sources.emplace_back(src.m_active_wrkpl, src.m_uuid);
            }
            finish_add_group(&group);
        });
    }
    else if (group_type == Group::Type::SOLID_MODEL_OPERATION) {
        auto &group = doc.insert_group<GroupSolidModelOperation>(UUID::random(), current_group.m_uuid);
        new_group = &group;
    }
    else if (group_type == Group::Type::CLONE) {
        if (!current_group.m_active_wrkpl) {
            m_workspace_browser->show_toast(toast_prefix + "Current group needs an active workplane");
            return;
        }
        GroupClone &group = doc.insert_group<GroupClone>(UUID::random(), current_group.m_uuid);
        new_group = &group;
        group.m_source_group = current_group.m_uuid;
        group.m_source_wrkpl = current_group.m_active_wrkpl;
    }
    else if (group_type == Group::Type::PIPE) {
        if (!current_group.m_active_wrkpl) {
            m_workspace_browser->show_toast(toast_prefix + "Current group needs an active workplane");
            return;
        }
        auto &group = doc.insert_group<GroupPipe>(UUID::random(), current_group.m_uuid);
        new_group = &group;
        group.m_wrkpl = current_group.m_active_wrkpl;
        group.m_source_group = current_group.m_uuid;
    }
    if (new_group && group_type == Group::Type::EXTRUDE) {
        m_extrude_editing = true;
    }
    if (new_group && (add_group_mode == WorkspaceBrowserAddGroupMode::WITH_BODY
                      || group_type == Group::Type::EXTRUDE)) {
        // A connected extrusion modifies the existing body.  It must not
        // become a new body merely because the tool was launched in
        // WITH_BODY mode.
        const auto *solid_group = dynamic_cast<const IGroupSolidModel *>(new_group);
        const auto *source_sketch = dynamic_cast<const GroupSketch *>(&current_group);
        const bool attached_to_existing_body = source_sketch && source_sketch->m_attached_to_face;
        const bool connected_extrusion = solid_group
                                          && (solid_group->get_operation()
                                                      == IGroupSolidModel::Operation::DIFFERENCE
                                              || attached_to_existing_body);
        const bool starts_first_body = group_type == Group::Type::EXTRUDE
                                       && !SolidModel::get_last_solid_model(doc, current_group,
                                                                             SolidModel::IncludeGroup::YES);
        if (!connected_extrusion && (add_group_mode == WorkspaceBrowserAddGroupMode::WITH_BODY || starts_first_body))
            new_group->m_body.emplace();
    }
    finish_add_group(new_group);
}

void Editor::finish_sketch_plane_selection(const UUID &plane)
{
    if (!m_selecting_sketch_plane)
        return;

    auto &doc = m_core.get_current_document();
    if (!doc.m_entities.contains(plane))
        return;
    auto &plane_entity = doc.get_entity<EntityWorkplane>(plane);
    if (plane_entity.m_group != doc.get_reference_group().m_uuid)
        return;

    auto &group = doc.insert_group<GroupSketch>(UUID::random(), m_sketch_plane_current_group);
    m_sketch_plane_created_group = group.m_uuid;
    group.m_active_wrkpl = plane;
    m_sketch_grid_offset.reset();
    get_current_document_view().m_group_views[group.m_uuid].m_visible = true;
    // A sketch attached to a solid face belongs to that face's body.  Do not
    // start a new body here, otherwise a following Difference extrusion has
    // no prior solid model to use as its boolean argument.

    m_selecting_sketch_plane = false;
    m_sketch_plane_grid = plane;
    // Capture the view immediately before this plane selection so Undo can
    // return here even when plane selection was re-entered by Undo.
    m_sketch_plane_previous_cam_quat = get_canvas().get_cam_quat();
    m_sketch_plane_previous_cam_distance = get_canvas().get_cam_distance();
    m_restore_sketch_plane_cam_on_undo = true;
    m_win.get_sketch_plane_selector().set_visible(false);
    get_canvas().set_selection_mode(SelectionMode::NORMAL);
    finish_add_group(&group);
    m_sketch_editing = true;
    update_sketch_mode_ui();
    auto camera_quat = plane_entity.m_normal;
    if (plane == doc.get_reference_group().get_workplane_zx_uuid()) {
        // XZ has +Y as its positive normal, but open the sketch from Front.
        camera_quat = glm::quatLookAt(glm::dvec3(0, 1, 0), glm::dvec3(0, 0, 1));
    }
    get_canvas().set_cam_distance(default_sketch_camera_distance, Canvas::ZoomCenter::SCREEN);
    debug_log(DebugCategory::UI,
              std::format("sketch active distance={:.6f}", get_canvas().get_cam_distance()));
    get_canvas().animate_to_cam_quat(glm::quat(camera_quat));
    canvas_update();
    Glib::signal_idle().connect_once([this] {
        if (m_sketch_editing)
            get_canvas().set_cam_distance(default_sketch_camera_distance, Canvas::ZoomCenter::SCREEN);
    });
}

void Editor::finish_sketch_face_selection(const UUID &solid_group_uuid, unsigned int face_idx)
{
    if (!m_selecting_sketch_plane)
        return;

    auto &doc = m_core.get_current_document();
    face::Faces transformed_step_faces;
    const face::Faces *faces_ptr = nullptr;
    EntitySTEP *step_entity = nullptr;
    if (doc.get_groups().contains(solid_group_uuid)) {
        auto &source_group = doc.get_group(solid_group_uuid);
        const auto *solid_group = dynamic_cast<const IGroupSolidModel *>(&source_group);
        if (!solid_group || !solid_group->get_solid_model())
            return;
        // Solid-model face selection identifies the imported group rather
        // than the EntitySTEP directly.  Keep that group before dependent
        // features so its body is available to a following cut.
        const auto body_group = source_group.find_body(doc).group.m_uuid;
        if (source_group.m_uuid != body_group) {
            doc.reorder_group(source_group.m_uuid, body_group);
            doc.set_group_generate_pending(source_group.m_uuid);
        }
        faces_ptr = &solid_group->get_solid_model()->m_faces;
    }
    else if ((step_entity = doc.get_entity_ptr<EntitySTEP>(solid_group_uuid)) && step_entity->m_imported) {
        // Selecting a face of an imported STEP body makes that body the
        // solid-model target for features created from the sketch.  This is
        // also needed for documents created before imported bodies were
        // included in the solid-model chain by default.
        step_entity->m_include_in_solid_model = true;
        // If the import was created while a later feature was selected, it
        // can otherwise appear after the extrusion in feature order.  Move
        // it to the beginning of its body before creating the attached
        // sketch so it becomes the boolean argument.
        const auto body_group = doc.get_group(step_entity->m_group).find_body(doc).group.m_uuid;
        if (step_entity->m_group != body_group)
            doc.reorder_group(step_entity->m_group, body_group);
        doc.set_group_generate_pending(step_entity->m_group);
        transformed_step_faces = step_entity->m_imported->result.faces;
        for (auto &step_face : transformed_step_faces) {
            for (auto &vertex : step_face.vertices) {
                const auto transformed = step_entity->transform({vertex.x, vertex.y, vertex.z});
                vertex = {static_cast<float>(transformed.x), static_cast<float>(transformed.y),
                          static_cast<float>(transformed.z)};
            }
            for (auto &normal_vertex : step_face.normals) {
                const auto transformed = glm::rotate(step_entity->m_normal,
                                                     glm::dvec3(normal_vertex.x, normal_vertex.y, normal_vertex.z));
                normal_vertex = {static_cast<float>(transformed.x), static_cast<float>(transformed.y),
                                 static_cast<float>(transformed.z)};
            }
        }
        faces_ptr = &transformed_step_faces;
    }
    else {
        return;
    }

    const auto &faces = *faces_ptr;
    if (face_idx >= faces.size())
        return;
    const auto &face = faces.at(face_idx);
    if (face.vertices.size() < 3 || face.normals.empty())
        return;

    const auto &vertex = face.vertices.front();
    const glm::dvec3 origin{vertex.x, vertex.y, vertex.z};
    const auto &face_normal = face.normals.front();
    const glm::dvec3 raw_normal{face_normal.x, face_normal.y, face_normal.z};
    if (glm::length(raw_normal) <= 1e-9)
        return;

    // The tessellator has already applied the OCC face orientation.  Do not
    // infer this from the model bounding-box center: that fails for faces
    // exposed by cuts and for faces on cavities.
    auto normal = glm::normalize(raw_normal);
    // Select the side of the face that is currently being viewed.  This is
    // important for boolean results: a cut can expose a face whose normal is
    // opposite to the side from which the user selected it, and the model
    // center is not a reliable way to resolve that (especially for cavities).
    const auto camera_normal = glm::dvec3(get_canvas().get_cam_normal());
    if (glm::dot(normal, camera_normal) < 0)
        normal = -normal;

    // Build one canonical view/basis for the selected face.  Using the first
    // polygon edge here makes the sketch orientation depend on OCC's vertex
    // ordering, which can make otherwise identical faces open differently.
    const glm::vec3 face_direction = glm::normalize(glm::vec3(normal));
    const glm::vec3 world_up{0, 0, 1};
    glm::vec3 camera_up;
    if (std::abs(face_direction.z) > 0.999f)
        // Keep Front (-Y) at the bottom of top and bottom views.
        camera_up = {0, 1, 0};
    else
        camera_up = world_up - face_direction * glm::dot(world_up, face_direction);
    if (glm::length(camera_up) < 1e-6f)
        camera_up = {0, 1, 0};
    else
        camera_up = glm::normalize(camera_up);
    const auto camera_right = glm::normalize(glm::cross(camera_up, face_direction));
    const auto camera_quat = glm::quat_cast(glm::mat3(camera_right, camera_up, face_direction));

    // A face sketch must follow the body whose face was selected.  The
    // active group can still be Reference when plane selection began, which
    // would otherwise place the sketch before the imported STEP feature.
    const UUID sketch_after_group = doc.get_groups().contains(solid_group_uuid)
                                            ? solid_group_uuid
                                            : (step_entity ? step_entity->m_group : m_sketch_plane_current_group);
    auto &group = doc.insert_group<GroupSketch>(UUID::random(), sketch_after_group);
    m_sketch_plane_created_group = group.m_uuid;
    group.m_attached_to_face = true;
    bool added = false;
    auto &workplane = doc.get_or_add_entity<EntityWorkplane>(UUID::random(), &added);
    workplane.m_origin = origin;
    // The outward face normal is also the default sketch/extrusion direction,
    // so a new extrusion grows away from the supporting solid.  Match the
    // workplane's local X/Y axes to the camera's right/up axes so the sketch
    // grid has the same orientation as the face view.
    workplane.m_normal = quat_from_uv(glm::dvec3(camera_right), glm::dvec3(camera_up));
    workplane.m_group = group.m_uuid;
    workplane.m_kind = ItemKind::USER;
    group.m_active_wrkpl = workplane.m_uuid;
    {
        auto &lock_rotation = doc.add_constraint<ConstraintLockRotation>(UUID::random());
        lock_rotation.m_group = group.m_uuid;
        lock_rotation.m_entity = workplane.m_uuid;
    }
    get_current_document_view().m_group_views[group.m_uuid].m_visible = true;
    if (m_sketch_plane_add_group_mode == WorkspaceBrowserAddGroupMode::WITH_BODY && !group.m_attached_to_face)
        group.m_body.emplace();

    m_selecting_sketch_plane = false;
    m_sketch_plane_grid = workplane.m_uuid;
    double face_projection = glm::dot(normal, origin);
    double far_projection = face_projection;
    for (const auto &other_face : faces) {
        for (const auto &v : other_face.vertices)
            far_projection = std::min(far_projection, glm::dot(normal, glm::dvec3{v.x, v.y, v.z}));
    }
    // Render the grid just beyond the far side of the solid.  For example,
    // a face at 0 with 20 mm of material behind it gets a grid at 20.0001.
    m_sketch_grid_offset = normal * (far_projection - face_projection) - normal * 1e-4;
    // Capture the view immediately before this face/plane selection so Undo
    // can restore it on repeated plane-selection cycles.
    m_sketch_plane_previous_cam_quat = get_canvas().get_cam_quat();
    m_sketch_plane_previous_cam_distance = get_canvas().get_cam_distance();
    m_restore_sketch_plane_cam_on_undo = true;
    m_win.get_sketch_plane_selector().set_visible(false);
    get_canvas().set_selection_mode(SelectionMode::NORMAL);
    finish_add_group(&group);
    m_sketch_editing = true;
    update_sketch_mode_ui();
    get_canvas().set_cam_distance(default_sketch_camera_distance, Canvas::ZoomCenter::SCREEN);
    debug_log(DebugCategory::UI,
              std::format("sketch active on face distance={:.6f}", get_canvas().get_cam_distance()));
    get_canvas().animate_to_cam_quat(camera_quat);
    canvas_update();
    Glib::signal_idle().connect_once([this] {
        if (m_sketch_editing)
            get_canvas().set_cam_distance(default_sketch_camera_distance, Canvas::ZoomCenter::SCREEN);
    });
}

void Editor::finish_sketch()
{
    if (!m_core.has_documents() || !force_end_tool())
        return;
    auto &doc = m_core.get_current_document();
    auto &sketch = doc.get_group(m_core.get_current_group());
    if (sketch.get_type() != Group::Type::SKETCH)
        return;
    if (sketch.m_active_wrkpl) {
        auto &workplane = doc.get_entity<EntityWorkplane>(sketch.m_active_wrkpl);
        if (workplane.m_visible) {
            workplane.m_visible = false;
            m_core.set_needs_save();
        }
    }
    m_sketch_finished_for_undo = sketch.m_uuid;
    m_sketch_finished_return_cam_distance = m_sketch_plane_previous_cam_distance;
    debug_log(DebugCategory::UI,
              std::format("sketch finish distance={:.6f} saved_start={}", get_canvas().get_cam_distance(),
                           m_sketch_plane_previous_cam_distance
                                   ? std::format("{:.6f}", *m_sketch_plane_previous_cam_distance)
                                   : std::string("none")));
    m_sketch_editing = false;
    if (m_sketch_previous_visibility) {
        get_current_document_view().m_group_views[sketch.m_uuid].m_visible = *m_sketch_previous_visibility;
        m_sketch_previous_visibility.reset();
    }
    update_sketch_mode_ui();
    canvas_update();
    if (m_sketch_plane_previous_cam_quat) {
        const auto previous_cam_distance = m_sketch_plane_previous_cam_distance;
        // Finish Sketch must return to the exact pre-sketch view. Stop any
        // active camera animation first so its old zoom target cannot
        // overwrite the saved distance on a later frame.
        get_canvas().stop_camera_animation();
        get_canvas().set_cam_quat(*m_sketch_plane_previous_cam_quat);
        if (previous_cam_distance)
            get_canvas().set_cam_distance(*previous_cam_distance, Canvas::ZoomCenter::SCREEN);
        m_sketch_plane_previous_cam_quat.reset();
        m_sketch_plane_previous_cam_distance.reset();
    }
}

void Editor::finish_extrusion()
{
    if (!m_core.has_documents() || !force_end_tool())
        return;
    auto &doc = m_core.get_current_document();
    auto &current_group = doc.get_group(m_core.get_current_group());
    if (current_group.get_type() != Group::Type::EXTRUDE)
        return;
    const auto &extrude = dynamic_cast<const GroupExtrude &>(current_group);
    // Once the extrusion is committed, hide its source sketch overlay.  The
    // finished solid remains visible through the extrusion group itself.
    if (doc.get_groups().contains(extrude.m_source_group))
        get_current_document_view().m_group_views[extrude.m_source_group].m_visible = false;
    m_extrude_dragging = false;
    m_extrude_editing = false;
    m_win.hide_extrude_dimension();
    m_solid_model_edge_select_mode = false;
    // The extrusion editor adds transient line/icon geometry to render
    // chunks. Rebuild all chunks when it closes so none of that preview can
    // survive into the committed model view.
    m_update_groups_after = UUID();
    update_sketch_mode_ui();
    canvas_update();
    // Refresh the tree after hiding the source sketch so its checkbox matches
    // the committed visibility state instead of the extrusion preview state.
    if (m_workspace_browser)
        m_workspace_browser->update_documents(get_current_document_views());
}

void Editor::finish_add_group(Group *new_group)
{
    DUNE3D_TRACE(DebugCategory::MODEL);
    if (!new_group)
        return;
    CanvasUpdater canvas_updater{*this};
    auto &doc = m_core.get_current_document();
    auto group_type = new_group->get_type();
    new_group->m_name = doc.find_next_group_name(group_type);
    doc.set_group_generate_pending(new_group->m_uuid);
    m_core.set_needs_save();
    m_core.rebuild("add group");
    m_workspace_browser->update_documents(get_current_document_views());
    update_timeline();
    if (group_type == Group::Type::EXTRUDE)
        set_current_group(new_group->m_uuid);
    m_workspace_browser->select_group(new_group->m_uuid);
    if (any_of(group_type, Group::Type::FILLET, Group::Type::CHAMFER)) {
        trigger_action(ToolID::SELECT_EDGES);
    }
    else if (group_type == Group::Type::PIPE) {
        trigger_action(ToolID::SELECT_SPINE_ENTITIES);
    }
    else if (group_type == Group::Type::EXTRUDE) {
        // The group rebuild can finish before the extrusion editor state is
        // reflected in the ribbon and canvas. Refresh both so the handle and
        // dimension textbox appear immediately.
        update_sketch_mode_ui();
        canvas_update();
    }
}

void Editor::on_delete_current_group()
{
    if (m_core.tool_is_active())
        return;

    auto &doc = m_core.get_current_document();

    auto &group = doc.get_group(m_core.get_current_group());
    if (!group.can_delete())
        return;

    UUID previous_group;
    previous_group = doc.get_group_rel(group.m_uuid, -1);
    if (!previous_group)
        previous_group = doc.get_group_rel(group.m_uuid, 1);

    if (!previous_group)
        return;
    doc.set_group_generate_pending(previous_group);

    {
        ItemsToDelete items;
        items.groups = {group.m_uuid};
        const auto items_initial = items;
        auto extra_items = doc.get_additional_items_to_delete(items);
        items.append(extra_items);
        show_delete_items_popup(items_initial, items);
        doc.delete_items(items);
    }

    get_current_document_view().m_current_group = previous_group;
    m_core.set_current_group(previous_group);
    m_workspace_browser->update_documents(get_current_document_views());
    set_current_group(previous_group);

    m_core.set_needs_save();
    m_core.rebuild("delete group");
}

void Editor::on_move_group(Document::MoveGroup op)
{
    if (m_core.tool_is_active())
        return;
    CanvasUpdater canvas_updater{*this};
    auto &doc = m_core.get_current_document();
    auto group = m_core.get_current_group();

    UUID group_after = doc.get_group_after(group, op);
    if (!group_after) {
        m_workspace_browser->show_toast("Couldn't move group");
        return;
    }

    if (!doc.reorder_group(group, group_after)) {
        m_workspace_browser->show_toast("Couldn't move group");
        return;
    }
    m_core.set_needs_save();
    m_core.rebuild("reorder_group");
    m_workspace_browser->update_documents(get_current_document_views());
}

void Editor::on_workspace_browser_document_checked(const UUID &uu_doc, bool checked)
{
    CanvasUpdater canvas_updater{*this};
    get_current_document_views()[uu_doc].m_document_is_visible = checked;
    m_workspace_browser->update_current_group(get_current_document_views());
    update_workspace_view_names();
}

void Editor::on_workspace_browser_origin_checked(const UUID &uu_doc, bool checked)
{
    auto &doc = m_core.get_idocument_info(uu_doc).get_document();
    auto &reference = doc.get_reference_group();
    if (reference.m_show_origin == checked)
        return;
    reference.m_show_origin = checked;
    m_core.set_needs_save();
    m_workspace_browser->update_current_group(get_current_document_views());
    canvas_update();
}

void Editor::on_workspace_browser_sketches_checked(const UUID &uu_doc, bool checked)
{
    CanvasUpdater canvas_updater{*this};
    auto &doc = m_core.get_idocument_info(uu_doc).get_document();
    auto &doc_view = get_current_document_views()[uu_doc];
    for (const auto *group : doc.get_groups_sorted()) {
        if (group->get_type() == Group::Type::SKETCH)
            doc_view.m_group_views[group->m_uuid].m_visible = checked;
    }
    m_workspace_browser->set_sketches_checked(uu_doc, checked);
    m_workspace_browser->update_current_group(get_current_document_views());
}

void Editor::on_workspace_browser_group_checked(const UUID &uu_doc, const UUID &uu_group, bool checked)
{
    CanvasUpdater canvas_updater{*this};
    auto &doc = m_core.get_idocument_info(uu_doc).get_document();
    auto &group = doc.get_group(uu_group);
    if (group.m_body.has_value())
        get_current_document_views()[uu_doc].m_body_views[uu_group].m_visible = checked;
    else
        get_current_document_views()[uu_doc].m_group_views[uu_group].m_visible = checked;
    debug_log(DebugCategory::UI,
              "checkbox doc=" + static_cast<std::string>(uu_doc) + " group=" + static_cast<std::string>(uu_group)
                      + " checked=" + std::to_string(checked));
    m_workspace_browser->update_current_group(get_current_document_views());
}

void Editor::on_workspace_browser_body_checked(const UUID &uu_doc, const UUID &uu_group, bool checked)
{
    DUNE3D_TRACE(DebugCategory::UI);
    CanvasUpdater canvas_updater{*this};
    debug_log(DebugCategory::UI,
              "body checkbox doc=" + static_cast<std::string>(uu_doc) + " body="
                      + static_cast<std::string>(uu_group) + " checked=" + std::to_string(checked));
    get_current_document_views()[uu_doc].m_body_views[uu_group].m_visible = checked;
    m_workspace_browser->set_body_checked(uu_doc, uu_group, checked);
    m_workspace_browser->update_current_group(get_current_document_views());
}

void Editor::on_workspace_browser_body_solid_model_checked(const UUID &uu_doc, const UUID &uu_group, bool checked)
{
    CanvasUpdater canvas_updater{*this};
    get_current_document_views()[uu_doc].m_body_views[uu_group].m_solid_model_visible = checked;
    m_workspace_browser->update_current_group(get_current_document_views());
}

void Editor::on_workspace_browser_activate_link(const std::string &link)
{
    const auto j = json::parse(link);
    const auto op = j.at("op").get<std::string>();
    if (op == "find-redundant-constraints") {
        m_properties_notebook->set_current_page(m_properties_notebook->page_num(*m_constraints_box));
        m_constraints_box->set_redundant_only();
    }
    else if (op == "undo") {
        if (!m_core.tool_is_active())
            trigger_action(ActionID::UNDO);
    }
}

void Editor::on_workspace_browser_rename_body(const UUID &uu_doc, const UUID &uu_group)
{
    auto &doc = m_core.get_idocument_info(uu_doc).get_document();
    auto &body = doc.get_group(uu_group).m_body.value();

    auto win = new RenameWindow("Rename body");
    win->set_text(body.m_name);
    win->set_transient_for(m_win);
    win->set_modal(true);
    win->present();
    win->signal_changed().connect([this, win, &body, &doc, &uu_group] {
        auto txt = win->get_text();
        body.m_name = txt;

        doc.set_group_update_solid_model_pending(uu_group);
        m_core.rebuild("rename body");
        canvas_update_keep_selection();
    });
}

void Editor::on_workspace_browser_set_body_color(const UUID &uu_doc, const UUID &uu_group)
{

    auto dia = Gtk::ColorDialog::create();
    dia->set_with_alpha(false);
    Color initial{.5, .5, .5};
    auto &doc = m_core.get_idocument_info(uu_doc).get_document();
    auto &body = doc.get_group(uu_group).m_body.value();

    if (body.m_color)
        initial = body.m_color.value();

    dia->choose_rgba(m_win, rgba_from_color(initial),
                     [this, dia, &body, &doc, &uu_group](Glib::RefPtr<Gio::AsyncResult> &result) {
                         try {
                             auto rgba = dia->choose_rgba_finish(result);
                             body.m_color = color_from_rgba(rgba);

                             doc.set_group_update_solid_model_pending(uu_group);
                             m_core.rebuild("set body color");
                             canvas_update_keep_selection();
                         }
                         catch (const Gtk::DialogError &err) {
                             // Can be thrown by dialog->open_finish(result).
                         }
                     });
}

void Editor::on_workspace_browser_reset_body_color(const UUID &uu_doc, const UUID &uu_group)
{
    auto &doc = m_core.get_idocument_info(uu_doc).get_document();
    doc.get_group(uu_group).m_body.value().m_color.reset();
    doc.set_group_update_solid_model_pending(uu_group);
    m_core.rebuild("reset body color");
    canvas_update_keep_selection();
}

} // namespace dune3d
