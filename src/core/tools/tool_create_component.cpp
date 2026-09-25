#include "tool_create_component.hpp"
#include "document/document.hpp"
#include "document/component.hpp"
#include "document/group/group_occurrence.hpp"
#include "document/group/group_reference.hpp"
#include "document/group/igroup_source_group.hpp"
#include "tool_common_impl.hpp"
#include <optional>

namespace dune3d {

// get_groups_by_body() returns its vector by value; a BodyGroups reference
// or pointer taken from it dangles the moment that temporary is destroyed.
// Return a copy instead -- safe, since BodyGroups::body is a reference into
// the (stable, long-lived) Group's own storage and groups holds pointers
// into that same storage, not into the temporary vector itself.
static std::optional<Document::BodyGroups> find_body_groups(Document &doc, const UUID &current_group)
{
    for (auto &bg : doc.get_groups_by_body()) {
        for (const auto *group : bg.groups) {
            if (group->m_uuid == current_group)
                return bg;
        }
    }
    return std::nullopt;
}

// get_groups_by_body()'s span folds groups that don't own their own body
// (e.g. a plain sketch) into whichever body-owning group's span was open at
// that point in the timeline -- for a fresh [Reference, Sketch, Extrude]
// document, that's the Reference group's span, not the Extrude's, since no
// body was open yet when the sketch was encountered. So a body's own
// BodyGroups::groups doesn't reliably include the sketch(es) it actually
// depends on. Expand to the transitive closure over
// IGroupSourceGroup::get_source_groups() (what GroupExtrude/GroupLoft/etc.
// actually use to record "this group needs that group to regenerate"), so
// the sketch comes along too -- otherwise the moved group ends up
// referencing a source group that doesn't exist in its new Document and
// generate()/update_pending() crashes.
static std::vector<UUID> collect_extraction_set(Document &doc, const Document::BodyGroups &bg)
{
    std::set<UUID> set;
    for (const auto *group : bg.groups)
        set.insert(group->m_uuid);

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto &uu : std::vector<UUID>(set.begin(), set.end())) {
            const auto &group = doc.get_group(uu);
            const auto *src = dynamic_cast<const IGroupSourceGroup *>(&group);
            if (!src)
                continue;
            for (const auto &req : src->get_source_groups(doc)) {
                if (req && doc.get_groups().contains(req) && !set.contains(req)) {
                    set.insert(req);
                    changed = true;
                }
            }
        }
    }
    set.erase(doc.get_reference_group().m_uuid);

    std::vector<UUID> ordered;
    for (const auto *group : doc.get_groups_sorted())
        if (set.contains(group->m_uuid))
            ordered.push_back(group->m_uuid);
    return ordered;
}

ToolBase::CanBegin ToolCreateComponent::can_begin()
{
    auto &doc = get_doc();
    auto bg = find_body_groups(doc, m_core.get_current_group());
    if (!bg || bg->groups.empty())
        return false;
    const auto type = bg->groups.front()->get_type();
    // The Reference group's own body holds the document's origin/workplanes
    // -- not something that makes sense to move into a component. An
    // occurrence's body is already a placed component instance; turning it
    // into a component of a component isn't supported by this first tool
    // (nothing stops nesting components themselves, just not through this
    // specific "componentize this body" action).
    if (type == Group::Type::REFERENCE || type == Group::Type::OCCURRENCE)
        return false;
    return true;
}

ToolResponse ToolCreateComponent::begin(const ToolArgs &args)
{
    auto &doc = get_doc();
    auto bg = find_body_groups(doc, m_core.get_current_group());
    if (!bg || bg->groups.empty())
        return ToolResponse::end();

    const auto group_uuids = collect_extraction_set(doc, *bg);
    if (group_uuids.empty())
        return ToolResponse::end();

    const auto after = doc.get_group_rel(group_uuids.front(), -1);
    const auto body_name = bg->body.m_name;

    // Components only ever live in the root's m_components (see Component's
    // own comment on why) -- doc may itself be a Component's own Document
    // while descended into one (Core::get_active_occurrence_path()), which
    // must never gain its own populated m_components map.
    auto &comp = m_core.get_root_document().add_component(UUID::random(), doc.get_reference_group().m_uuid);
    comp.m_name = body_name;
    doc.extract_groups(group_uuids, comp.m_document);
    comp.m_document.set_group_solve_pending(group_uuids.front());
    comp.m_document.update_pending();

    auto &occ_group = doc.insert_group<GroupOccurrence>(UUID::random(), after);
    occ_group.m_component = comp.m_uuid;
    occ_group.m_body.emplace();
    occ_group.m_body->m_name = body_name;
    occ_group.m_name = body_name;

    doc.set_group_generate_pending(occ_group.m_uuid);

    return ToolResponse::commit_and_set_current_group(occ_group.m_uuid);
}

ToolResponse ToolCreateComponent::update(const ToolArgs &args)
{
    return ToolResponse();
}

} // namespace dune3d
