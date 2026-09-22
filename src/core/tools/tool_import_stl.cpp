#include "tool_import_stl.hpp"
#include "document/document.hpp"
#include "document/group/group.hpp"
#include "document/group/group_sketch.hpp"
#include "document/group/group_step.hpp"
#include "document/entity/entity_step.hpp"
#include "document/constraint/constraint_lock_rotation.hpp"
#include "editor/editor_interface.hpp"
#include "dialogs/dialogs.hpp"
#include "util/fs_util.hpp"
#include "util/glm_util.hpp"
#include "util/action_label.hpp"
#include "tool_common_impl.hpp"
#include "core/tool_data_path.hpp"
#include <gtkmm.h>

namespace dune3d {

ToolBase::CanBegin ToolImportSTL::can_begin()
{
    return can_create_entity() || get_group().get_type() == Group::Type::REFERENCE;
}

ToolResponse ToolImportSTL::begin(const ToolArgs &args)
{
    auto dialog = Gtk::FileDialog::create();
    auto dir = m_core.get_current_document_directory();
    if (!dir.empty())
        dialog->set_initial_folder(Gio::File::create_for_path(path_to_string(dir)));

    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    auto filter = Gtk::FileFilter::create();
    filter->set_name("Mesh (STL, 3MF)");
    filter->add_pattern("*.stl");
    filter->add_pattern("*.STL");
    filter->add_pattern("*.3mf");
    filter->add_pattern("*.3MF");
    filters->append(filter);
    dialog->set_filters(filters);

    dialog->open(m_intf.get_dialogs().get_parent(), [this, dialog](const Glib::RefPtr<Gio::AsyncResult> &result) {
        try {
            auto file = dialog->open_finish(result);
            m_intf.tool_update_data(std::make_unique<ToolDataPath>(path_from_string(file->get_path())));
        }
        catch (const Gtk::DialogError &) {
            m_intf.tool_update_data(std::make_unique<ToolDataPath>());
        }
        catch (const Glib::Error &) {
            m_intf.tool_update_data(std::make_unique<ToolDataPath>());
        }
    });
    return ToolResponse();
}

void ToolImportSTL::update_tip()
{
    if (!m_mesh)
        return;
    std::vector<ActionLabelInfo> actions;
    actions.emplace_back(InToolActionID::LMB, "place");
    actions.emplace_back(InToolActionID::RMB, "cancel");
    actions.emplace_back(InToolActionID::ROTATE_X, InToolActionID::ROTATE_Y, InToolActionID::ROTATE_Z, "rotate");
    m_intf.set_constraint_icons(m_intf.get_cursor_pos(), {NAN, NAN, NAN}, {});
    m_intf.tool_bar_set_actions(actions);
    m_intf.tool_bar_set_tool_tip("Place imported mesh");
}

ToolResponse ToolImportSTL::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::MOVE && m_mesh) {
        m_mesh->m_origin = m_intf.get_cursor_pos();
        set_first_update_group_current();
    }
    else if (args.type == ToolEventType::DATA) {
        if (auto data = dynamic_cast<const ToolDataPath *>(args.data.get())) {
            if (data->path.empty())
                return ToolResponse::end();
            const auto after_group = get_group().get_type() == Group::Type::REFERENCE
                                           ? get_group().find_body(get_doc()).group.m_uuid
                                           : get_group().m_uuid;
            auto &import_group = get_doc().insert_group<GroupStep>(UUID::random(), after_group);
            import_group.m_name = data->path.stem().string();
            import_group.m_body.emplace();
            m_mesh = &get_doc().add_entity<EntitySTEP>(UUID::random());
            m_mesh->m_group = import_group.m_uuid;
            get_doc().set_group_generate_pending(import_group.m_uuid);
            auto dir = m_core.get_current_document_directory();
            if (auto rel = get_relative_filename(data->path, dir))
                m_mesh->m_path = *rel;
            else
                m_mesh->m_path = data->path;
            m_mesh->update_imported(dir);
            m_mesh->m_origin = {0, 0, 0};
            m_mesh->m_include_in_solid_model = true;
            return ToolResponse::commit();
        }
    }
    else if (args.type == ToolEventType::ACTION && m_mesh) {
        switch (args.action) {
        case InToolActionID::LMB:
            if (m_lock_rotation) {
                auto &constraint = add_constraint<ConstraintLockRotation>();
                constraint.m_entity = m_mesh->m_uuid;
            }
            return ToolResponse::commit();
        case InToolActionID::RMB:
        case InToolActionID::CANCEL:
            return ToolResponse::revert();
        case InToolActionID::TOGGLE_LOCK_ROTATION_CONSTRAINT:
            m_lock_rotation = !m_lock_rotation;
            break;
        case InToolActionID::ROTATE_X:
        case InToolActionID::ROTATE_Y:
        case InToolActionID::ROTATE_Z: {
            glm::dvec3 axis(0);
            axis[static_cast<int>(args.action) - static_cast<int>(InToolActionID::ROTATE_X)] = 1;
            m_lock_rotation = true;
            m_mesh->m_normal = glm::angleAxis(M_PI / 2, axis) * m_mesh->m_normal;
            break;
        }
        default:
            break;
        }
    }
    update_tip();
    return ToolResponse();
}
} // namespace dune3d
