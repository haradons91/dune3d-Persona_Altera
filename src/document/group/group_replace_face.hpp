#pragma once
#include "group_face_operation.hpp"

namespace dune3d {

// Modify ribbon's "Replace Face": trims the previous body to a reference
// plane (m_reference_wrkpl, an existing EntityWorkplane picked via
// ToolSetReplaceFacePlane) in place of the one selected face's own
// surface. Only supports trimming material away (the reference plane
// must lie inward of the selected face's own plane) -- replacing with a
// farther-out plane would need to ADD material via a bounded
// extrusion+fuse, a materially different and harder operation not
// attempted in this first pass; see solid_model_replace_face.cpp's
// explicit delta check.
class GroupReplaceFace : public GroupFaceOperation {
public:
    explicit GroupReplaceFace(const UUID &uu);
    explicit GroupReplaceFace(const UUID &uu, const json &j);

    static constexpr Type s_type = Type::REPLACE_FACE;
    Type get_type() const override
    {
        return s_type;
    }

    UUID m_reference_wrkpl;

    json serialize() const override;
    std::unique_ptr<Group> clone() const override;

    void update_solid_model(const Document &doc) override;

    std::set<UUID> get_referenced_entities(const Document &doc) const override;
    std::set<UUID> get_required_entities(const Document &doc) const override;
};

} // namespace dune3d
