#pragma once
#include "group_face_operation.hpp"

namespace dune3d {

// Modify ribbon's "Draft": tapers the selected faces by m_angle (degrees)
// about their line of intersection with a fixed global XY neutral plane,
// pulling along global +Z (BRepOffsetAPI_DraftAngle) -- same fixed-origin
// convention as GroupScale's m_center, kept simple for this first pass
// rather than letting the user pick an arbitrary neutral plane/direction.
class GroupDraft : public GroupFaceOperation {
public:
    explicit GroupDraft(const UUID &uu);
    explicit GroupDraft(const UUID &uu, const json &j);

    static constexpr Type s_type = Type::DRAFT;
    Type get_type() const override
    {
        return s_type;
    }

    double m_angle = 5.0;

    json serialize() const override;
    std::unique_ptr<Group> clone() const override;

    void update_solid_model(const Document &doc) override;
};

} // namespace dune3d
