#pragma once

#include "tool_common.hpp"

namespace dune3d {

class ToolDrawThreeTangentCircle : public ToolCommon {
public:
    using ToolCommon::ToolCommon;
    ToolResponse begin(const ToolArgs &args) override;
    ToolResponse update(const ToolArgs &args) override;
    CanBegin can_begin() override;

private:
    class EntityCircle2D *m_circle = nullptr;
};

} // namespace dune3d
