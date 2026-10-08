#pragma once

#include "tool_common.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include <glm/glm.hpp>
#include <map>

namespace dune3d {

class Entity;

// Creates a new parallel entity for each selected EntityLine2D/
// EntityArc2D/EntityCircle2D, offset by a distance that's live-tracked
// from the cursor (like ToolSketchFillet's radius-drag) rather than a
// modal dialog -- an inline, draggable on-canvas textbox
// (show/update/position_offset_dimension) shows the current value and
// lets you type an exact one instead. Positive grows/extends outward,
// negative shrinks/moves inward. The originals are left untouched
// (Fusion-style sketch Offset), with ConstraintParallel (lines) /
// ConstraintRadius (arcs/circles, pinned to the resulting value) tying
// each new entity back to its source.
class ToolSketchOffset : public ToolCommon {
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
    std::map<Entity *, Entity *> m_source_to_preview;
    Entity *m_reference_source = nullptr;
    double m_last_distance = 0;
    bool m_distance_locked = false;

    void apply_offset(double distance);
    // Distance from the cursor to m_reference_source's own curve (signed:
    // positive outside/further, negative inside/closer), plus the 2D
    // base/tip points the on-canvas dimension line is drawn between.
    double compute_live_distance(glm::dvec2 cursor, glm::dvec2 &base, glm::dvec2 &tip);
    void update_offset_ui(bool first = false);
};

} // namespace dune3d
