#pragma once

#include "tool_common.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include <glm/glm.hpp>

namespace dune3d {

class EntityLine2D;

// Chamfer submenu's distance-distance and distance-angle variants
// (SKETCH_CHAMFER_DISTANCE_DISTANCE / SKETCH_CHAMFER_DISTANCE_ANGLE).
// Requires two selected EntityLine2D sharing a corner, same
// corner-detection as ToolSketchFillet/ToolSketchChamfer. Mouse-drag
// live-tracks whichever side is "active" (an on-screen preview line
// connects the two chamfer points), with an inline two-field textbox
// (reusing the plain rectangle-dimensions width/height + Tab-switch
// mechanism ToolDrawRectangle already uses) -- Tab locks the active
// side's current value and switches the mouse-drag + keyboard focus to
// the other side.
class ToolSketchChamferAdvanced : public ToolCommon {
public:
    using ToolCommon::ToolCommon;

    ToolResponse begin(const ToolArgs &args) override;
    ToolResponse update(const ToolArgs &args) override;
    std::set<InToolActionID> get_actions() const override
    {
        using I = InToolActionID;
        return {I::LMB, I::RMB, I::CANCEL};
    }
    bool is_specific() override
    {
        return true;
    }
    CanBegin can_begin() override;

private:
    EntityLine2D *m_line1 = nullptr;
    EntityLine2D *m_line2 = nullptr;
    EntityLine2D *m_preview = nullptr;
    glm::dvec2 m_corner{};
    glm::dvec2 m_line1_dir{}, m_line2_dir{};
    int m_line1_corner = -1, m_line2_corner = -1;
    // Side 1's distance, and side 2's value -- a plain distance for the
    // Distance-Distance variant, an angle in degrees for Distance-Angle.
    double m_dist1 = 1.0;
    double m_dist2 = 1.0;
    bool m_active_is_first = true;

    bool setup_corner();
    glm::dvec2 point1() const;
    glm::dvec2 point2() const;
    void update_preview();
    void commit_chamfer();
};

} // namespace dune3d
