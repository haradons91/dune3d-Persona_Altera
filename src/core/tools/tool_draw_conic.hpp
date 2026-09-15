#pragma once

#include "tool_common.hpp"
#include "in_tool_action/in_tool_action.hpp"

namespace dune3d {

class ToolDrawConic : public ToolCommon {
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
    glm::dvec2 m_first_point;
    glm::dvec2 m_second_point;
    unsigned int m_points_placed = 0;
    class EntityBezier2D *m_curve = nullptr;
    glm::dvec2 get_cursor_pos_in_plane() const;
    void update_geometry(const glm::dvec2 &p);
};

} // namespace dune3d
