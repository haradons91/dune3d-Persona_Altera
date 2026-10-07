#pragma once

#include "tool_common.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include <glm/glm.hpp>

namespace dune3d {

class EntityLine2D;

// SKETCH_TRIM: hover-driven, like ToolSketchBreak -- hovering over a line
// that crosses another finds the nearest crossing to the cursor, previews
// the side of the line that would be removed (a construction-styled
// temporary line, so it renders visibly dashed/distinct), and LMB commits
// by truncating the real line's endpoint to that crossing.
//
// SKETCH_EXTEND is NOT yet rebuilt on this interactive pattern -- it still
// uses the older one-shot "two pre-selected lines" behavior (also still
// broken; a separate follow-up).
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
    // Old one-shot path, used only for SKETCH_EXTEND for now.
    ToolResponse begin_extend_one_shot();

    EntityLine2D *m_hovered_line = nullptr;
    EntityLine2D *m_other_line = nullptr;
    EntityLine2D *m_preview = nullptr;
    glm::dvec2 m_trim_point{};
    unsigned int m_trim_point_index = 0;

    bool update_preview();
};

} // namespace dune3d
