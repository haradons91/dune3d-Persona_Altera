#pragma once

#include "tool_common.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include <glm/glm.hpp>

namespace dune3d {

class Entity;
class EntityPoint2D;

// Hover-driven like ToolSketchTrimExtend -- no pre-selection needed.
// Splits whichever EntityLine2D/EntityArc2D is hovered into two at the
// point on the curve closest to the cursor (live-tracked on MOVE,
// shown with a preview point marker, committed on LMB).
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
    Entity *m_hovered_entity = nullptr;
    EntityPoint2D *m_preview = nullptr;
    glm::dvec2 m_break_point{};

    bool update_preview();
    glm::dvec2 compute_break_point(Entity &entity);
};

} // namespace dune3d
