#pragma once

#include "tool_common.hpp"
#include <glm/glm.hpp>

namespace dune3d {

class EntityLine2D;

// Chamfer submenu's distance-distance and distance-angle variants
// (SKETCH_CHAMFER_DISTANCE_DISTANCE / SKETCH_CHAMFER_DISTANCE_ANGLE),
// implemented as a separate tool from the existing equal-distance
// ToolSketchChamfer (a live cursor-drag tool) rather than parameterizing
// it, to avoid risking regressions in that already-working tool --
// this one uses the sequential-datum-dialog pattern instead, like every
// other new tool this pass. Requires two selected EntityLine2D sharing a
// corner, same corner-detection as ToolSketchFillet/ToolSketchChamfer.
class ToolSketchChamferAdvanced : public ToolCommon {
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
    enum class Stage { FIRST, SECOND };
    Stage m_stage = Stage::FIRST;
    double m_first_value = 0;

    EntityLine2D *m_line1 = nullptr;
    EntityLine2D *m_line2 = nullptr;
    glm::dvec2 m_corner{};
    glm::dvec2 m_line1_dir{}, m_line2_dir{};
    int m_line1_corner = -1, m_line2_corner = -1;

    bool setup_corner();
    void commit_chamfer(double first_value, double second_value);
};

} // namespace dune3d
