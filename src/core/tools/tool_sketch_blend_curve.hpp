#pragma once

#include "tool_common.hpp"
#include <glm/glm.hpp>

namespace dune3d {

class EntityLine2D;
class EntityBezier2D;

// Bridges a gap between two selected EntityLine2D endpoints with a
// tangent-continuous cubic Bezier, control-point "bulge" entered via the
// datum dialog (DatumUnit::RATIO, as a fraction of the chord length
// between the two connection points) -- structurally like
// ToolSketchFillet's two-line corner detection, but connecting the
// nearest pair of endpoints (which may be far apart, unlike Fillet's
// shared corner) rather than rounding a shared corner, and needing no
// trim step since it only adds a new entity.
class ToolSketchBlendCurve : public ToolCommon {
public:
    using ToolCommon::ToolCommon;

    ToolResponse begin(const ToolArgs &args) override;
    ToolResponse update(const ToolArgs &args) override;
    bool is_specific() override
    {
        return true;
    }
    CanBegin can_begin() override;

private:
    EntityLine2D *m_line1 = nullptr;
    EntityLine2D *m_line2 = nullptr;
    EntityBezier2D *m_preview = nullptr;
    glm::dvec2 m_p1{}, m_p2{};
    glm::dvec2 m_line1_dir{}, m_line2_dir{};
    double m_chord = 0;

    void apply_bulge(double bulge);
};

} // namespace dune3d
