#include "tool_insert_occurrence.hpp"
#include "document/document.hpp"
#include "document/component.hpp"
#include "document/group/group_occurrence.hpp"
#include "editor/editor_interface.hpp"
#include "dialogs/dialogs.hpp"
#include "widgets/select_component_dialog.hpp"
#include "core/tool_data_uuid.hpp"
#include "tool_common_impl.hpp"

namespace dune3d {

ToolBase::CanBegin ToolInsertOccurrence::can_begin()
{
    return get_doc().get_components().size() > 0;
}

ToolResponse ToolInsertOccurrence::begin(const ToolArgs &args)
{
    // Matches the existing SelectGroupDialog usage pattern (src/widgets/group_button.cpp):
    // a bare `new`, self-managed by GTK once shown, no explicit delete. Closing without
    // picking (X button/Esc) just leaves this tool active with nothing further happening --
    // the same level of polish as that precedent; not handled specially here.
    auto dialog = new SelectComponentDialog(get_doc());
    dialog->set_transient_for(m_intf.get_dialogs().get_parent());
    dialog->present();
    dialog->signal_changed().connect([this, dialog] {
        m_intf.tool_update_data(std::make_unique<ToolDataUUID>(dialog->get_selected_component()));
    });

    return ToolResponse();
}

ToolResponse ToolInsertOccurrence::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::DATA) {
        if (auto data = dynamic_cast<const ToolDataUUID *>(args.data.get())) {
            if (!data->uuid) {
                return ToolResponse::end();
            }
            auto &doc = get_doc();
            auto &occ_group = doc.insert_group<GroupOccurrence>(UUID::random(), m_core.get_current_group());
            occ_group.m_component = data->uuid;
            occ_group.m_body.emplace();
            occ_group.m_body->m_name = doc.get_component(data->uuid).m_name;
            occ_group.m_name = occ_group.m_body->m_name;
            doc.set_group_generate_pending(occ_group.m_uuid);
            return ToolResponse::commit_and_set_current_group(occ_group.m_uuid);
        }
    }
    return ToolResponse();
}

} // namespace dune3d
