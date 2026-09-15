#pragma once

#include "tool_common.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include <array>

namespace dune3d {

class ToolDrawEllipse : public ToolCommon {
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
    glm::dvec2 m_center;
    glm::dvec2 m_major_point;
    unsigned int m_points_placed = 0;
    std::array<class EntityBezier2D *, 4> m_segments = {nullptr, nullptr, nullptr, nullptr};
    glm::dvec2 get_cursor_pos_in_plane() const;
    void update_geometry(const glm::dvec2 &p);
};

} // namespace dune3d
