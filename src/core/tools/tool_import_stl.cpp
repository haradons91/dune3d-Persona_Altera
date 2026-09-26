#include "tool_import_stl.hpp"
#include "document/document.hpp"
#include "document/group/group.hpp"
#include "document/group/group_sketch.hpp"
#include "document/group/group_stl.hpp"
#include "document/group/group_threemf.hpp"
#include "document/entity/entity_stl.hpp"
#include "document/entity/entity_threemf.hpp"
#include "document/constraint/constraint_lock_rotation.hpp"
#include "editor/editor_interface.hpp"
#include "dialogs/dialogs.hpp"
#include "util/fs_util.hpp"
#include "util/glm_util.hpp"
#include "util/action_label.hpp"
#include "tool_common_impl.hpp"
#include "core/tool_data_path.hpp"
#include <gtkmm.h>
#include <cctype>

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
    if (!has_mesh())
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
    if (args.type == ToolEventType::MOVE && has_mesh()) {
        const auto pos = m_intf.get_cursor_pos();
        if (m_stl)
            m_stl->m_origin = pos;
        else
            m_threemf->m_origin = pos;
        set_first_update_group_current();
    }
    else if (args.type == ToolEventType::DATA) {
        if (auto data = dynamic_cast<const ToolDataPath *>(args.data.get())) {
            if (data->path.empty())
                return ToolResponse::end();
            const auto after_group = get_group().get_type() == Group::Type::REFERENCE
                                           ? get_group().find_body(get_doc()).group.m_uuid
                                           : get_group().m_uuid;
            auto dir = m_core.get_current_document_directory();
            const auto path_for_entity = [&]() -> std::filesystem::path {
                if (auto rel = get_relative_filename(data->path, dir))
                    return *rel;
                return data->path;
            }();

            auto ext = data->path.extension().string();
            for (auto &c : ext)
                c = std::tolower(static_cast<unsigned char>(c));
            if (ext == ".3mf") {
                auto &import_group = get_doc().insert_group<GroupThreeMF>(UUID::random(), after_group);
                import_group.m_name = data->path.stem().string();
                import_group.m_body.emplace();
                m_threemf = &get_doc().add_entity<EntityThreeMF>(UUID::random());
                m_threemf->m_group = import_group.m_uuid;
                get_doc().set_group_generate_pending(import_group.m_uuid);
                m_threemf->m_path = path_for_entity;
                m_threemf->update_imported(dir);
                m_threemf->m_origin = {0, 0, 0};
            }
            else {
                auto &import_group = get_doc().insert_group<GroupSTL>(UUID::random(), after_group);
                import_group.m_name = data->path.stem().string();
                import_group.m_body.emplace();
                m_stl = &get_doc().add_entity<EntitySTL>(UUID::random());
                m_stl->m_group = import_group.m_uuid;
                get_doc().set_group_generate_pending(import_group.m_uuid);
                m_stl->m_path = path_for_entity;
                m_stl->update_imported(dir);
                m_stl->m_origin = {0, 0, 0};
            }
            return ToolResponse::commit();
        }
    }
    else if (args.type == ToolEventType::ACTION && has_mesh()) {
        switch (args.action) {
        case InToolActionID::LMB:
            if (m_lock_rotation) {
                auto &constraint = add_constraint<ConstraintLockRotation>();
                constraint.m_entity = m_stl ? m_stl->m_uuid : m_threemf->m_uuid;
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
            const auto rot = glm::angleAxis(M_PI / 2, axis);
            if (m_stl)
                m_stl->m_normal = rot * m_stl->m_normal;
            else
                m_threemf->m_normal = rot * m_threemf->m_normal;
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
