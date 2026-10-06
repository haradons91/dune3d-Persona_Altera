#pragma once
#include "group_face_operation.hpp"

namespace dune3d {

// Modify ribbon's "Align": rigidly moves the whole previous body so the
// one selected planar face becomes coplanar with, and facing along, a
// fixed global target plane (the XY plane, +Z outward) -- the same
// fixed-origin convention GroupScale/GroupDraft already use, kept simple
// for this first pass rather than letting the user pick an arbitrary
// target plane or a second body/face to align to.
class GroupAlign : public GroupFaceOperation {
public:
    using GroupFaceOperation::GroupFaceOperation;

    static constexpr Type s_type = Type::ALIGN;
    Type get_type() const override
    {
        return s_type;
    }

    void update_solid_model(const Document &doc) override;
    std::unique_ptr<Group> clone() const override;
};

} // namespace dune3d
