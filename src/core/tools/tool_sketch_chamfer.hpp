#pragma once

#include "tool_common.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include <glm/glm.hpp>

namespace dune3d {

class EntityLine2D;

class ToolSketchChamfer : public ToolCommon {
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
    EntityLine2D *m_preview_line = nullptr;
    glm::dvec2 m_corner{};
    glm::dvec2 m_line1_direction{};
    glm::dvec2 m_line2_direction{};
    int m_line1_corner = -1;
    int m_line2_corner = -1;
    double m_max_distance = 0;

    bool select_line(EntityLine2D *&line);
    bool setup_corner();
    void update_chamfer();
    double get_distance();
};

} // namespace dune3d
