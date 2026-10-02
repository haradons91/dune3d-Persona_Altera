#pragma once
#include "group_face_operation.hpp"

namespace dune3d {

// Modify ribbon's "Remove": deletes the selected faces (e.g. a fillet's
// rounding, a boss, a hole) from the previous body and heals the
// surrounding faces to fill the gap (BRepAlgoAPI_Defeaturing).
class GroupRemove : public GroupFaceOperation {
public:
    using GroupFaceOperation::GroupFaceOperation;

    static constexpr Type s_type = Type::REMOVE;
    Type get_type() const override
    {
        return s_type;
    }

    void update_solid_model(const Document &doc) override;
    std::unique_ptr<Group> clone() const override;
};

} // namespace dune3d
