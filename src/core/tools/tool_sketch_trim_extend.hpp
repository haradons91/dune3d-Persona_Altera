#pragma once

#include "tool_common.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include <glm/glm.hpp>

namespace dune3d {

class EntityLine2D;

// Backs both SKETCH_TRIM and SKETCH_EXTEND, hover-driven like
// ToolSketchBreak -- no pre-selection needed. Hovering a line looks for
// the nearest OTHER line it meets:
// - Trim: a genuine crossing within the hovered line's own current
//   extent (excluding its own existing endpoints -- see
//   update_preview()'s comment on rectangle corners).
// - Extend: the nearest line the hovered line would meet if extended
//   past whichever end the cursor is closer to. If the target point
//   falls within the other line's own existing segment (a "T" against a
//   line that's already long enough), only the hovered line moves,
//   joined with ConstraintPointOnLine. If the other line ALSO falls
//   short of the same point (an open "corner" -- two lines that don't
//   yet reach each other), BOTH lines are extended to meet there,
//   joined with ConstraintPointsCoincident, with a second preview line
//   shown for the other line's own extension.
class ToolSketchTrimExtend : public ToolCommon {
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
        return false;
    }
    CanBegin can_begin() override;

private:
    EntityLine2D *m_hovered_line = nullptr;
    EntityLine2D *m_other_line = nullptr;
    EntityLine2D *m_preview = nullptr;
    EntityLine2D *m_preview2 = nullptr;
    glm::dvec2 m_trim_point{};
    unsigned int m_trim_point_index = 0;
    // Extend's "corner" case only: which of m_other_line's own endpoints
    // (1 or 2) also needs to move to m_trim_point. 0 means a plain
    // T-shape target -- m_other_line doesn't move.
    unsigned int m_other_point_index = 0;

    bool update_preview();
    bool find_trim_target(EntityLine2D &line, glm::dvec2 d, double cursor_t, EntityLine2D *&best_other,
                          double &best_t);
    bool find_extend_target(EntityLine2D &line, glm::dvec2 d, double cursor_t, EntityLine2D *&best_other,
                            double &best_t, unsigned int &best_other_point_index);
};

} // namespace dune3d
