#pragma once
#include "entityt.hpp"
#include <glm/glm.hpp>
#include "ientity_normal.hpp"
#include "ientity_movable3d.hpp"

namespace dune3d {
// Places one instance of a Component (see document/component.hpp) at a rigid
// transform. Modeled closely on EntityDocument (an external linked
// document's placement), minus the external-file/anchor machinery that
// doesn't apply to an in-project component. No scale/mirror -- rigid
// transform only, matching the current feature scope; joints/mates are a
// deliberately separate future addition.
class EntityOccurrence : public EntityT<EntityOccurrence>, public IEntityNormal, public IEntityMovable3D {
public:
    explicit EntityOccurrence(const UUID &uu);
    explicit EntityOccurrence(const UUID &uu, const json &j);
    static constexpr Type s_type = Type::OCCURRENCE;
    json serialize() const override;

    double get_param(unsigned int point, unsigned int axis) const override;
    void set_param(unsigned int point, unsigned int axis, double value) override;

    glm::dvec3 get_point(unsigned int point, const Document &doc) const override;
    bool is_valid_point(unsigned int point) const override;

    // Which Component this instances: a key into the *root* Document's
    // m_components (see Component's own comment on why that's always the
    // root's map, never a nested one).
    UUID m_component;

    glm::dvec3 m_origin = {0, 0, 0};
    glm::dquat m_normal;

    // Optional per-instance display override (e.g. "Bracket:2"); falls back
    // to the Component's own name when empty.
    std::string m_instance_label;

    glm::dvec3 transform(glm::dvec3 p) const;

    void move(const Entity &last, const glm::dvec3 &delta, unsigned int point) override;

    std::string get_point_name(unsigned int point) const override;

    void set_normal(const glm::dquat &q) override
    {
        m_normal = q;
    }
    glm::dquat get_normal() const override
    {
        return m_normal;
    }
};

} // namespace dune3d
