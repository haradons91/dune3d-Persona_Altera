#pragma once
#include "group_face_operation.hpp"
#include <glm/glm.hpp>

namespace dune3d {

// Modify ribbon's "Split Face": divides each selected face into two faces
// along its intersection with a plane (m_plane_point/m_plane_normal),
// leaving the body a single solid with one more face -- unlike Split
// Body, this never creates a second body (BRepAlgoAPI_Section for the
// trimmed intersection edge + BRepFeat_SplitShape::Add to add it onto the
// face).
class GroupSplitFace : public GroupFaceOperation {
public:
    explicit GroupSplitFace(const UUID &uu);
    explicit GroupSplitFace(const UUID &uu, const json &j);

    static constexpr Type s_type = Type::SPLIT_FACE;
    Type get_type() const override
    {
        return s_type;
    }

    glm::dvec3 m_plane_point = {0, 0, 0};
    glm::dvec3 m_plane_normal = {0, 0, 1};

    json serialize() const override;
    std::unique_ptr<Group> clone() const override;

    void update_solid_model(const Document &doc) override;
};

} // namespace dune3d
