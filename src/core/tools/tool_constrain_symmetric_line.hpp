#include "tool_common_constrain.hpp"
#include "util/selection_util.hpp"

namespace dune3d {

class ToolConstrainSymmetricLine : public ToolCommonConstrain {
public:
    using ToolCommonConstrain::ToolCommonConstrain;

    ToolResponse begin(const ToolArgs &args) override;
    ToolResponse update(const ToolArgs &args) override;
    CanBegin can_begin() override;

    // Now lives in the ribbon's Constrain dropdown -- keep it out of the
    // right-click menu to avoid duplicating it there.
    bool is_specific() override
    {
        return false;
    }

    bool constraint_is_in_workplane() override
    {
        return true;
    }

private:
    TwoPoints m_points;
};
} // namespace dune3d
