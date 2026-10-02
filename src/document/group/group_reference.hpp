#pragma once
#include "group.hpp"
#include "igroup_generate.hpp"
#include "document/entity/entity_workplane.hpp"
#include "canvas/projection.hpp"
#include <glm/gtx/quaternion.hpp>

namespace dune3d {

class EntityWorkplane;

// A saved camera position/orientation, named by the user and persisted with
// the document -- same fields as WorkspaceView's own (unnamed, per-tab,
// not-saved-standalone) camera state, so restoring one is exactly the same
// handful of Canvas setters set_current_workspace_view() already uses.
struct NamedView {
    UUID uuid;
    std::string name;
    glm::dvec3 center = {0, 0, 0};
    float cam_distance = 100;
    CanvasProjection projection = CanvasProjection::ORTHO;
    glm::dquat cam_quat;

    json serialize() const;
    explicit NamedView(const json &j);
    NamedView() = default;
};

class GroupReference : public Group, public IGroupGenerate {
public:
    explicit GroupReference(const UUID &uu);
    explicit GroupReference(const UUID &uu, const json &j);
    static constexpr Type s_type = Type::REFERENCE;
    Type get_type() const override
    {
        return s_type;
    }
    json serialize(const Document &doc) const override;
    std::unique_ptr<Group> clone() const override;

    virtual void generate(Document &doc) override;

    bool m_show_xy = false;
    bool m_show_yz = false;
    bool m_show_zx = false;
    bool m_show_origin = true;

    glm::dvec2 m_xy_size = {EntityWorkplane::s_default_size, EntityWorkplane::s_default_size};
    glm::dvec2 m_yz_size = {EntityWorkplane::s_default_size, EntityWorkplane::s_default_size};
    glm::dvec2 m_zx_size = {EntityWorkplane::s_default_size, EntityWorkplane::s_default_size};

    std::vector<NamedView> m_named_views;

    UUID get_workplane_xy_uuid() const;
    UUID get_workplane_yz_uuid() const;
    UUID get_workplane_zx_uuid() const;

    bool can_delete() const override
    {
        return false;
    }

    bool can_create_entity() const override
    {
        return false;
    }

    bool can_create_constraint() const override
    {
        return false;
    }

    bool can_have_active_workplane() const override
    {
        return false;
    }

private:
    EntityWorkplane &add_workplane(Document &doc, const UUID &uu, const glm::dquat &normal,
                                   const glm::dvec2 &size) const;
};

} // namespace dune3d
