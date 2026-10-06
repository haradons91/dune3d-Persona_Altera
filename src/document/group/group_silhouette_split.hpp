#pragma once
#include "group_face_operation.hpp"
#include <glm/glm.hpp>

namespace dune3d {

// Modify ribbon's "Silhouette Split": splits the selected face along the
// previous body's own silhouette (its outline as seen along m_direction,
// HLRBRep_Algo) projected onto that face (BRepProj_Projection) -- e.g. a
// cylindrical boss standing on a flat face casts a circular silhouette
// when viewed down its own axis, splitting whatever face that outline is
// projected onto. Like Split Face, this never creates a second body --
// the result is still one solid, just with the outline added as new
// edges/faces.
class GroupSilhouetteSplit : public GroupFaceOperation {
public:
    explicit GroupSilhouetteSplit(const UUID &uu);
    explicit GroupSilhouetteSplit(const UUID &uu, const json &j);

    static constexpr Type s_type = Type::SILHOUETTE_SPLIT;
    Type get_type() const override
    {
        return s_type;
    }

    glm::dvec3 m_direction = {0, 0, -1};

    json serialize() const override;
    std::unique_ptr<Group> clone() const override;

    void update_solid_model(const Document &doc) override;
};

} // namespace dune3d
