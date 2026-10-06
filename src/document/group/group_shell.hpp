#pragma once
#include "group_face_operation.hpp"

namespace dune3d {

// Modify ribbon's "Shell": hollows out the previous body, leaving the
// selected faces open (removed entirely) and the rest of the body as a
// wall of m_thickness (BRepOffsetAPI_MakeThickSolid).
class GroupShell : public GroupFaceOperation {
public:
    explicit GroupShell(const UUID &uu);
    explicit GroupShell(const UUID &uu, const json &j);

    static constexpr Type s_type = Type::SHELL;
    Type get_type() const override
    {
        return s_type;
    }

    double m_thickness = 1.0;

    json serialize() const override;
    std::unique_ptr<Group> clone() const override;

    void update_solid_model(const Document &doc) override;
};

} // namespace dune3d
