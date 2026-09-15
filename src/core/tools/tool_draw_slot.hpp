#pragma once

#include "tool_common.hpp"
#include "in_tool_action/in_tool_action.hpp"

namespace dune3d {

class ToolDrawSlot : public virtual ToolCommon {
public:
    using ToolCommon::ToolCommon;

    ToolResponse begin(const ToolArgs &args) override;
    ToolResponse update(const ToolArgs &args) override;
    std::set<InToolActionID> get_actions() const override
    {
        return {InToolActionID::LMB, InToolActionID::CANCEL, InToolActionID::RMB};
    }
    CanBegin can_begin() override;

private:
    const class EntityWorkplane *m_wrkpl = nullptr;
    enum class Mode { CENTER_TO_CENTER, OVERALL, CENTER_POINT, THREE_POINT_ARC, CENTER_POINT_ARC };
    Mode m_mode = Mode::CENTER_TO_CENTER;
    glm::dvec2 m_first_point;
    glm::dvec2 m_second_point;
    glm::dvec2 m_third_point;
    unsigned int m_points_placed = 0;
    class EntityLine2D *m_line_a = nullptr;
    class EntityLine2D *m_line_b = nullptr;
    class EntityArc2D *m_arc_a = nullptr;
    class EntityArc2D *m_arc_b = nullptr;

    glm::dvec2 get_cursor_pos_in_plane() const;
    void update_geometry(const glm::dvec2 &p);
};

} // namespace dune3d
