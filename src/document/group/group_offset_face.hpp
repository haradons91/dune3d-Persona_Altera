#pragma once
#include "group_face_operation.hpp"

namespace dune3d {

// Modify ribbon's "Offset Face": moves the selected faces along their own
// normal by m_offset, re-deriving the adjacent faces so the body stays a
// single closed solid (BRepOffset_MakeOffset::SetOffsetOnFace, Thickening
// disabled -- unlike Shell, which deliberately builds two parallel shells).
class GroupOffsetFace : public GroupFaceOperation {
public:
    explicit GroupOffsetFace(const UUID &uu);
    explicit GroupOffsetFace(const UUID &uu, const json &j);

    static constexpr Type s_type = Type::OFFSET_FACE;
    Type get_type() const override
    {
        return s_type;
    }

    double m_offset = 1.0;

    json serialize() const override;
    std::unique_ptr<Group> clone() const override;

    void update_solid_model(const Document &doc) override;
};

} // namespace dune3d
