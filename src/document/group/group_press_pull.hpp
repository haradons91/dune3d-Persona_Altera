#pragma once
#include "group_face_operation.hpp"

namespace dune3d {

// Modify ribbon's "Press/Pull": moves the selected faces along their own
// normal by m_offset, re-deriving adjacent faces so the body stays a
// single closed solid. Mechanically identical to GroupOffsetFace
// (BRepOffset_MakeOffset::SetOffsetOnFace) -- Fusion-style CAD tools
// expose this same operation under two different ribbon entry points, so
// this is kept as its own group/action rather than folded into Offset
// Face, matching the ribbon's intended menu shape.
class GroupPressPull : public GroupFaceOperation {
public:
    explicit GroupPressPull(const UUID &uu);
    explicit GroupPressPull(const UUID &uu, const json &j);

    static constexpr Type s_type = Type::PRESS_PULL;
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
