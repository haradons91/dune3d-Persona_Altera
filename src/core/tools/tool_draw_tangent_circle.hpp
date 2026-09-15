#pragma once

#include "tool_common.hpp"

namespace dune3d {

class ToolDrawTangentCircle : public ToolCommon {
public:
    using ToolCommon::ToolCommon;
    ToolResponse begin(const ToolArgs &args) override;
    ToolResponse update(const ToolArgs &args) override;
    CanBegin can_begin() override;

private:
    const class EntityWorkplane *m_wrkpl = nullptr;
    const class EntityLine2D *m_line_a = nullptr;
    const class EntityLine2D *m_line_b = nullptr;
    class EntityCircle2D *m_circle = nullptr;
    glm::dvec2 m_intersection;
    glm::dvec2 m_bisector_a;
    glm::dvec2 m_bisector_b;
    void update_circle(const glm::dvec2 &p);
};

} // namespace dune3d
