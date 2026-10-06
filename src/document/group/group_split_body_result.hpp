#pragma once
#include "group.hpp"
#include "igroup_solid_model.hpp"
#include "igroup_source_group.hpp"
#include <memory>
#include <list>

namespace dune3d {

class Document;
class SolidModel;

// The complementary half of a "Split Body" operation -- always created
// paired with, and immediately after, a GroupSplitBody (which computes the
// +normal-side piece and keeps it in the original body). This group has
// m_body set at creation (so it's its own new body, per
// Document::get_groups_by_body()) and reads the plane straight off
// m_source_group rather than storing its own copy.
class GroupSplitBodyResult : public Group, public IGroupSolidModel, public IGroupSourceGroup {
public:
    explicit GroupSplitBodyResult(const UUID &uu);
    explicit GroupSplitBodyResult(const UUID &uu, const json &j);
    static constexpr Type s_type = Type::SPLIT_BODY_RESULT;
    Type get_type() const override
    {
        return s_type;
    }

    UUID m_source_group;

    std::set<UUID> get_source_groups(const Document &doc) const override
    {
        return {m_source_group};
    }
    // Deleting the GroupSplitBody this depends on deletes this group too --
    // it can't compute anything without its sibling's plane parameters.
    std::set<UUID> get_required_groups(const Document &doc) const override
    {
        return {m_source_group};
    }

    json serialize() const override;
    std::unique_ptr<Group> clone() const override;

    Operation get_operation() const override
    {
        return Operation::UNION;
    }
    void set_operation(Operation op) override
    {
    }

    const SolidModel *get_solid_model() const override;
    void update_solid_model(const Document &doc) override;

    std::shared_ptr<const SolidModel> m_solid_model;
    std::list<GroupStatusMessage> m_split_body_messages;
    std::list<GroupStatusMessage> get_messages() const override;
};

} // namespace dune3d
