#pragma once
#include "tool_common.hpp"

namespace dune3d {

// Commits whatever EntityWorkplane is currently selected in the canvas as
// the current GroupReplaceFace's reference plane -- same single-shot,
// no-interactive-loop shape as ToolSetWorkplane, just targeting a
// GroupReplaceFace-specific field instead of the generic
// Group::m_active_wrkpl.
class ToolSetReplaceFacePlane : public ToolCommon {
public:
    using ToolCommon::ToolCommon;

    ToolResponse begin(const ToolArgs &args) override;
    ToolResponse update(const ToolArgs &args) override;
    bool is_specific() override
    {
        return false;
    }
    CanBegin can_begin() override;
};
} // namespace dune3d
