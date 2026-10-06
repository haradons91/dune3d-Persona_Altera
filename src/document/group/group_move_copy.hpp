#pragma once
#include "group.hpp"
#include "igroup_solid_model.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <list>

namespace dune3d {

class Document;
class SolidModel;

// Modify ribbon's "Move/Copy": translates and rotates the previous body as
// a whole (no face selection needed, like GroupScale). Rotation is about
// the global origin along m_rotation_axis by m_rotation_angle (degrees),
// applied before the translation.
//
// m_copy (duplicating the body into a separate new body, as opposed to
// moving it in place) is not implemented yet -- that needs document/body
// management this first pass doesn't touch -- so it's reported as an
// explicit error rather than silently behaving like a plain move.
class GroupMoveCopy : public Group, public IGroupSolidModel {
public:
    explicit GroupMoveCopy(const UUID &uu);
    explicit GroupMoveCopy(const UUID &uu, const json &j);
    static constexpr Type s_type = Type::MOVE_COPY;
    Type get_type() const override
    {
        return s_type;
    }

    glm::dvec3 m_translation = {0, 0, 0};
    glm::dvec3 m_rotation_axis = {0, 0, 1};
    double m_rotation_angle = 0;
    bool m_copy = false;

    json serialize() const override;
    std::unique_ptr<Group> clone() const override;

    // Same fixed-operation convention as GroupScale/GroupSimplify -- only
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
    std::list<GroupStatusMessage> m_move_copy_messages;
    std::list<GroupStatusMessage> get_messages() const override;
};

} // namespace dune3d
