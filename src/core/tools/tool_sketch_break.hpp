#pragma once

#include "tool_common.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include <glm/glm.hpp>

namespace dune3d {

class EntityLine2D;
class EntityArc2D;
class Entity;

// Splits one selected EntityLine2D/EntityArc2D into two at the point on
// the curve closest to the cursor (live-tracked on MOVE, committed on
// LMB) -- the one tool in this batch that genuinely needs cursor
// tracking, since the split point isn't derivable from a fixed numeric
// parameter or a second entity's geometry the way every other tool here
// is.
class ToolSketchBreak : public ToolCommon {
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
    Entity *m_entity = nullptr;
    glm::dvec2 m_break_point{};

    glm::dvec2 compute_break_point();
};

} // namespace dune3d
