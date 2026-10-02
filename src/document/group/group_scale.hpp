#pragma once
#include "group.hpp"
#include "igroup_solid_model.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <list>

namespace dune3d {

class Document;
class SolidModel;

// Modify ribbon's "Scale": uniformly scales the previous body by m_factor
// about m_center. No selection needed (the whole previous body is scaled),
// unlike Fillet/Chamfer.
class GroupScale : public Group, public IGroupSolidModel {
public:
    explicit GroupScale(const UUID &uu);
    explicit GroupScale(const UUID &uu, const json &j);
    static constexpr Type s_type = Type::SCALE;
    Type get_type() const override
    {
        return s_type;
    }

    double m_factor = 1.0;
    glm::dvec3 m_center = {0, 0, 0};

    json serialize() const override;
    std::unique_ptr<Group> clone() const override;

    // Same fixed-operation convention as GroupSimplify/GroupLocalOperation --
    // Scale only ever has the one previous body as input.
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
    std::list<GroupStatusMessage> m_scale_messages;
    std::list<GroupStatusMessage> get_messages() const override;
};

} // namespace dune3d
