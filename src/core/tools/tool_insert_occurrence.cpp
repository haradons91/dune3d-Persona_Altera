#include "tool_insert_occurrence.hpp"
#include "document/document.hpp"
#include "document/component.hpp"
#include "document/entity/entity_occurrence.hpp"
#include "document/group/group_occurrence.hpp"
#include "editor/editor_interface.hpp"
#include "dialogs/dialogs.hpp"
#include "widgets/select_component_dialog.hpp"
#include "core/tool_data_uuid.hpp"
#include "tool_common_impl.hpp"
#include <vector>

namespace dune3d {

namespace {
// Every component_uu (and everything it, in turn, places an occurrence of)
// reachable from component_uu -- i.e. "components that would end up nested
// inside a new occurrence of component_uu". Used to refuse placing an
// occurrence that would make a component contain itself, directly or
// transitively.
std::set<UUID> collect_contained_components(Document &root, const UUID &component_uu)
{
    std::set<UUID> out;
    std::vector<UUID> stack{component_uu};
    while (!stack.empty()) {
        auto uu = stack.back();
        stack.pop_back();
        if (!out.insert(uu).second)
            continue;
        auto *comp = root.get_component_ptr(uu);
        if (!comp)
            continue;
        for (const auto &[euu, en] : comp->m_document.m_entities) {
            if (auto *occ = dynamic_cast<const EntityOccurrence *>(en.get()))
                stack.push_back(occ->m_component);
        }
    }
    return out;
}

// The chain of Components the user is currently "inside" (see
// Core::get_active_occurrence_path()), outermost first. Empty if editing
// the root directly -- nothing can create a cycle there, since the root
// isn't itself a component that could end up contained in one.
std::vector<UUID> ancestor_components(Document &root, const std::vector<UUID> &path)
{
    std::vector<UUID> out;
    Document *cur = &root;
    for (const auto &uu : path) {
        auto &occ = cur->get_entity<EntityOccurrence>(uu);
        out.push_back(occ.m_component);
        auto &comp = root.get_component<Component>(occ.m_component);
        cur = &comp.m_document;
    }
    return out;
}
} // namespace

ToolBase::CanBegin ToolInsertOccurrence::can_begin()
{
    // Components only ever live in the root's m_components (see
    // Component's own comment on why) -- get_doc() is the *active*
    // document, which is empty of components while descended into one, so
    // this must check the true root regardless of descend state.
    return m_core.get_root_document().get_components().size() > 0;
}

ToolResponse ToolInsertOccurrence::begin(const ToolArgs &args)
{
    // Matches the existing SelectGroupDialog usage pattern (src/widgets/group_button.cpp):
    // a bare `new`, self-managed by GTK once shown, no explicit delete. Closing without
    // picking (X button/Esc) just leaves this tool active with nothing further happening --
    // the same level of polish as that precedent; not handled specially here.
    auto dialog = new SelectComponentDialog(m_core.get_root_document());
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
            auto &root = m_core.get_root_document();
            const auto ancestors = ancestor_components(root, m_core.get_active_occurrence_path());
            if (!ancestors.empty()) {
                const auto contained = collect_contained_components(root, data->uuid);
                for (const auto &ancestor : ancestors) {
                    if (contained.contains(ancestor)) {
                        m_intf.tool_bar_flash(
                                "Can't place this here -- it would make a component contain itself");
                        return ToolResponse::end();
                    }
                }
            }

            auto &doc = get_doc();
            auto &occ_group = doc.insert_group<GroupOccurrence>(UUID::random(), m_core.get_current_group());
            occ_group.m_component = data->uuid;
            occ_group.m_body.emplace();
            occ_group.m_body->m_name = root.get_component(data->uuid).m_name;
            occ_group.m_name = occ_group.m_body->m_name;
            doc.set_group_generate_pending(occ_group.m_uuid);
            return ToolResponse::commit_and_set_current_group(occ_group.m_uuid);
        }
    }
    return ToolResponse();
}

} // namespace dune3d
