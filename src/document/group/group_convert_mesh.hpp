#pragma once
#include "group.hpp"
#include "igroup_solid_model.hpp"
#include "igroup_source_group.hpp"

namespace dune3d {

// Converts an imported reference-only mesh (EntitySTL/EntityThreeMF, owned
// by m_source_group) into a real solid-model Body via one of two sewing
// algorithms -- see solid_model_convert_mesh.cpp for why there are two and
// why a failure is surfaced as an error rather than silently accepted.
class GroupConvertMesh : public Group, public IGroupSolidModel, public IGroupSourceGroup {
public:
    explicit GroupConvertMesh(const UUID &uu);
    explicit GroupConvertMesh(const UUID &uu, const json &j);
    static constexpr Type s_type = Type::CONVERT_MESH;
    Type get_type() const override
    {
        return s_type;
    }
    json serialize() const override;
    std::unique_ptr<Group> clone() const override;

    // The GroupSTL/GroupThreeMF whose entity is converted -- set once at
    // creation, not user-editable afterward.
    UUID m_source_group;
    std::set<UUID> get_source_groups(const Document &doc) const override
    {
        return {m_source_group};
    }
    std::set<UUID> get_referenced_groups(const Document &doc) const override;
    std::set<UUID> get_required_groups(const Document &doc) const override;

    enum class Algorithm {
        DIRECT,
        MERGE_FACES,
        DECIMATE_SEW,
        WELD_SEW,
        DECIMATE_MERGE_FACES,
        CONVEX_HULL,
        BOUNDING_BOX,
    };
    Algorithm m_algorithm = Algorithm::DIRECT;
    unsigned int m_decimate_target_faces = 1500;
    double m_weld_tolerance = 0.01;

    std::shared_ptr<const SolidModel> m_solid_model;

    // Always starts a fresh body -- never combines with a prior one.
    Operation m_operation = Operation::UNION;
    Operation get_operation() const override
    {
        return m_operation;
    }
    void set_operation(Operation op) override
    {
    }

    const SolidModel *get_solid_model() const override;
    void update_solid_model(const Document &doc) override;
};

} // namespace dune3d
