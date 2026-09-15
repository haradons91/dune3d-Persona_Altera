#pragma once

#include "tool_common.hpp"

namespace dune3d {

class ToolProjectSketchGeometry : public ToolCommon {
public:
    using ToolCommon::ToolCommon;

    ToolResponse begin(const ToolArgs &args) override;
    ToolResponse update(const ToolArgs &args) override;
    CanBegin can_begin() override;
};

} // namespace dune3d
