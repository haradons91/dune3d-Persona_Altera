#pragma once
#include "group.hpp"
#include "igroup_solid_model.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <list>

namespace dune3d {

class Document;
class SolidModel;

// Modify ribbon's "Split Body": cuts the previous body by a plane
// (m_plane_point/m_plane_normal), keeping the half-space on the +normal
// side in THIS body (BRepAlgoAPI_Common against a half-space solid seeded
// from the plane) -- the other half is computed by the paired
// GroupSplitBodyResult group, which this group's creation always inserts
// immediately after itself in a new body. m_result_group is that sibling's
// UUID, kept only so this group's editor can also mark the sibling's solid
// model pending when the plane changes (the sibling's own generation logic
// reads the plane straight off this group instead).
class GroupSplitBody : public Group, public IGroupSolidModel {
public:
    explicit GroupSplitBody(const UUID &uu);
    explicit GroupSplitBody(const UUID &uu, const json &j);
    static constexpr Type s_type = Type::SPLIT_BODY;
    Type get_type() const override
    {
        return s_type;
    }

    glm::dvec3 m_plane_point = {0, 0, 0};
    glm::dvec3 m_plane_normal = {0, 0, 1};
    UUID m_result_group;

    json serialize() const override;
    std::unique_ptr<Group> clone() const override;

    // Same fixed-operation convention as GroupScale/GroupDraft -- only
    // ever has the one previous body as input.
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

    // Deleting either half of the pair deletes both -- they're one atomic
    // operation, and GroupSplitBodyResult can't function without this
    // group's plane parameters.
    std::set<UUID> get_required_groups(const Document &doc) const override;
};

} // namespace dune3d
