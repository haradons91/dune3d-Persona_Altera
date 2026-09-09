#include "editor.hpp"
#include "workspace_browser.hpp"
#include "dune3d_appwindow.hpp"
#include "widgets/constraints_box.hpp"
#include "document/group/all_groups.hpp"
#include "widgets/sketch_plane_selector.hpp"
#include "document/entity/entity_workplane.hpp"
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

namespace dune3d {
using json = nlohmann::json;

void Editor::init_workspace_browser()
{
    m_workspace_browser = Gtk::make_managed<WorkspaceBrowser>(m_core);
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
            if (!m_sketch_editing)
                m_sketch_plane_previous_cam_quat = get_canvas().get_cam_quat();
            m_sketch_editing = true;
            auto &sketch = m_core.get_current_document().get_group(uu_group);
            if (sketch.m_active_wrkpl) {
                auto &workplane = m_core.get_current_document().get_entity<EntityWorkplane>(sketch.m_active_wrkpl);
                workplane.m_visible = true;
                auto camera_quat = workplane.m_normal;
                const auto &reference = m_core.get_current_document().get_reference_group();
                if (sketch.m_active_wrkpl == reference.get_workplane_yz_uuid())
                    camera_quat = workplane.m_normal
                                  * glm::angleAxis(-static_cast<double>(M_PI) / 2, glm::dvec3(0, 0, 1));
                else if (sketch.m_active_wrkpl == reference.get_workplane_zx_uuid())
                    camera_quat = workplane.m_normal
                                  * glm::angleAxis(static_cast<double>(M_PI) / 2, glm::dvec3(0, 0, 1));
                get_canvas().animate_to_cam_quat(glm::quat(camera_quat));
            }
        }
        else if (type == Group::Type::EXTRUDE) {
            m_sketch_editing = false;
            m_extrude_editing = false;
        }
        else {
            return;
        }
        update_sketch_mode_ui();
        canvas_update();
    });
    m_workspace_browser->signal_add_group().connect(sigc::mem_fun(*this, &Editor::on_add_group));
    m_workspace_browser->signal_delete_current_group().connect(sigc::mem_fun(*this, &Editor::on_delete_current_group));
    m_workspace_browser->signal_move_group().connect(sigc::mem_fun(*this, &Editor::on_move_group));
    m_workspace_browser->signal_document_checked().connect(
            sigc::mem_fun(*this, &Editor::on_workspace_browser_document_checked));
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

    m_core.signal_rebuilt().connect([this] {
        Glib::signal_idle().connect_once([this] {
            if (!m_current_workspace_view)
                return;
            m_workspace_browser->update_documents(m_workspace_views.at(m_current_workspace_view).m_documents);
        });
    });

    m_win.get_left_bar().set_start_child(*m_workspace_browser);
}

void Editor::on_workspace_browser_group_selected(const UUID &uu_doc, const UUID &uu_group)
{
    if (m_core.tool_is_active())
        return;
    m_sketch_editing = false;
    auto &idoc = m_core.get_current_idocument_info();
    if (idoc.get_uuid() == uu_doc && idoc.get_current_group() == uu_group)
        return;
    m_core.set_current_document(uu_doc);
    m_workspace_views.at(m_current_workspace_view).m_current_document = uu_doc;
    update_version_info();
    get_current_document_view().m_current_group = uu_group;
    set_current_group(uu_group);
}

void Editor::on_add_group(Group::Type group_type, WorkspaceBrowserAddGroupMode add_group_mode)
{
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
        m_sketch_plane_previous_cam_quat = get_canvas().get_cam_quat();
        m_restore_sketch_plane_cam_on_undo = false;
        m_sketch_plane_created_group.reset();
        m_sketch_plane_current_group = current_group.m_uuid;
        m_sketch_plane_add_group_mode = add_group_mode;
        get_canvas().grab_focus();

        for (const auto &selection : get_canvas().get_selection()) {
            if (selection.type == SelectableRef::Type::SOLID_MODEL_FACE) {
                get_canvas().set_selection({}, false);
                finish_sketch_face_selection(selection.item, selection.point);
                return;
            }
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
        for (const auto &selection : get_canvas().get_selection()) {
            if (selection.type == SelectableRef::Type::SKETCH_PROFILE
                && selection.item == current_group.m_uuid) {
                group.m_source_path = selection.point;
                break;
            }
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
    if (new_group && add_group_mode == WorkspaceBrowserAddGroupMode::WITH_BODY)
        new_group->m_body.emplace();
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
    get_current_document_view().m_group_views[group.m_uuid].m_visible = true;
    if (m_sketch_plane_add_group_mode == WorkspaceBrowserAddGroupMode::WITH_BODY)
        group.m_body.emplace();

    m_selecting_sketch_plane = false;
    m_sketch_plane_grid = plane;
    // Capture the view immediately before this plane selection so Undo can
    // return here even when plane selection was re-entered by Undo.
    m_sketch_plane_previous_cam_quat = get_canvas().get_cam_quat();
    m_restore_sketch_plane_cam_on_undo = true;
    m_win.get_sketch_plane_selector().set_visible(false);
    get_canvas().set_selection_mode(SelectionMode::NORMAL);
    finish_add_group(&group);
    m_sketch_editing = true;
    update_sketch_mode_ui();
    auto camera_quat = plane_entity.m_normal;
    if (plane == doc.get_reference_group().get_workplane_yz_uuid()) {
        camera_quat = plane_entity.m_normal
                      * glm::angleAxis(-static_cast<double>(M_PI) / 2, glm::dvec3(0, 0, 1));
    }
    else if (plane == doc.get_reference_group().get_workplane_zx_uuid()) {
        camera_quat = plane_entity.m_normal
                      * glm::angleAxis(static_cast<double>(M_PI) / 2, glm::dvec3(0, 0, 1));
    }
    get_canvas().animate_to_cam_quat(glm::quat(camera_quat));
    canvas_update();
}

void Editor::finish_sketch_face_selection(const UUID &solid_group_uuid, unsigned int face_idx)
{
    if (!m_selecting_sketch_plane)
        return;

    auto &doc = m_core.get_current_document();
    if (!doc.get_groups().contains(solid_group_uuid))
        return;
    const auto &source_group = doc.get_group(solid_group_uuid);
    const auto *solid_group = dynamic_cast<const IGroupSolidModel *>(&source_group);
    if (!solid_group || !solid_group->get_solid_model())
        return;

    const auto &faces = solid_group->get_solid_model()->m_faces;
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
    const glm::dvec3 normal = glm::normalize(raw_normal);

    glm::dvec3 u;
    for (size_t i = 1; i < face.vertices.size(); i++) {
        const auto &candidate = face.vertices.at(i);
        u = glm::dvec3{candidate.x, candidate.y, candidate.z} - origin;
        if (glm::length(u) > 1e-9)
            break;
    }
    if (glm::length(u) <= 1e-9)
        return;
    u = glm::normalize(u);
    const auto v = glm::normalize(glm::cross(normal, u));
    if (glm::length(v) <= 1e-9)
        return;

    auto &group = doc.insert_group<GroupSketch>(UUID::random(), m_sketch_plane_current_group);
    m_sketch_plane_created_group = group.m_uuid;
    bool added = false;
    auto &workplane = doc.get_or_add_entity<EntityWorkplane>(UUID::random(), &added);
    workplane.m_origin = origin;
    // Face normals are used for viewing the selected face, but the sketch
    // workplane normal is the default extrusion direction.  Reverse its
    // in-plane Y axis so a new extrusion grows away from the supporting
    // solid instead of into it.
    workplane.m_normal = quat_from_uv(u, -v);
    workplane.m_group = group.m_uuid;
    workplane.m_kind = ItemKind::USER;
    group.m_active_wrkpl = workplane.m_uuid;
    get_current_document_view().m_group_views[group.m_uuid].m_visible = true;
    if (m_sketch_plane_add_group_mode == WorkspaceBrowserAddGroupMode::WITH_BODY)
        group.m_body.emplace();

    m_selecting_sketch_plane = false;
    m_sketch_plane_grid = workplane.m_uuid;
    // Capture the view immediately before this face/plane selection so Undo
    // can restore it on repeated plane-selection cycles.
    m_sketch_plane_previous_cam_quat = get_canvas().get_cam_quat();
    m_restore_sketch_plane_cam_on_undo = true;
    m_win.get_sketch_plane_selector().set_visible(false);
    get_canvas().set_selection_mode(SelectionMode::NORMAL);
    finish_add_group(&group);
    m_sketch_editing = true;
    update_sketch_mode_ui();
    // View the selected face from its normal side. The workplane basis was
    // built from the face normal, so an additional 180-degree flip would
    // incorrectly show a top face from underneath.
    // Build the camera directly from the face normal.  Its local +Z axis is
    // the camera position direction, while local +Y is chosen from world up
    // so the object's physical bottom stays at the bottom of the view.
    const auto face_direction = glm::normalize(glm::vec3(normal));
    glm::vec3 camera_normal;
    if (std::abs(face_direction.z) > 0.999f) {
        double face_z = 0;
        for (const auto &v : face.vertices)
            face_z += v.z;
        face_z /= face.vertices.size();

        double model_min_z = std::numeric_limits<double>::max();
        double model_max_z = std::numeric_limits<double>::lowest();
        for (const auto &other_face : faces) {
            for (const auto &v : other_face.vertices) {
                model_min_z = std::min(model_min_z, static_cast<double>(v.z));
                model_max_z = std::max(model_max_z, static_cast<double>(v.z));
            }
        }
        camera_normal = face_z >= (model_min_z + model_max_z) / 2. ? glm::vec3(0, 0, 1)
                                                                  : glm::vec3(0, 0, -1);
    }
    else {
        // Side-face normals are oriented toward the opposite side for this
        // view convention.
        camera_normal = -face_direction;
    }
    const glm::vec3 world_up{0, 0, 1};
    glm::vec3 camera_up;
    if (std::abs(camera_normal.z) > 0.999f)
        // Keep Front (+X) at the bottom of both horizontal views.
        camera_up = {-1, 0, 0};
    else
        camera_up = world_up - camera_normal * glm::dot(world_up, camera_normal);
    if (glm::length(camera_up) < 1e-6f)
        camera_up = {0, 1, 0};
    else
        camera_up = glm::normalize(camera_up);
    const auto camera_right = glm::normalize(glm::cross(camera_up, camera_normal));
    const auto camera_quat = glm::quat_cast(glm::mat3(camera_right, camera_up, camera_normal));
    get_canvas().animate_to_cam_quat(camera_quat);
    canvas_update();
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
    m_sketch_editing = false;
    update_sketch_mode_ui();
    canvas_update();
    if (m_sketch_plane_previous_cam_quat) {
        get_canvas().animate_to_cam_quat(*m_sketch_plane_previous_cam_quat);
        m_sketch_plane_previous_cam_quat.reset();
    }
}

void Editor::finish_extrusion()
{
    if (!m_core.has_documents() || !force_end_tool())
        return;
    if (m_core.get_current_document().get_group(m_core.get_current_group()).get_type() != Group::Type::EXTRUDE)
        return;
    m_extrude_dragging = false;
    m_extrude_editing = false;
    update_sketch_mode_ui();
    canvas_update();
}

void Editor::finish_add_group(Group *new_group)
{
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
    m_workspace_browser->select_group(new_group->m_uuid);
    if (any_of(group_type, Group::Type::FILLET, Group::Type::CHAMFER)) {
        trigger_action(ToolID::SELECT_EDGES);
    }
    else if (group_type == Group::Type::PIPE) {
        trigger_action(ToolID::SELECT_SPINE_ENTITIES);
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

void Editor::on_workspace_browser_group_checked(const UUID &uu_doc, const UUID &uu_group, bool checked)
{
    CanvasUpdater canvas_updater{*this};
    get_current_document_views()[uu_doc].m_group_views[uu_group].m_visible = checked;
    m_workspace_browser->update_current_group(get_current_document_views());
}

void Editor::on_workspace_browser_body_checked(const UUID &uu_doc, const UUID &uu_group, bool checked)
{
    CanvasUpdater canvas_updater{*this};
    get_current_document_views()[uu_doc].m_body_views[uu_group].m_visible = checked;
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
