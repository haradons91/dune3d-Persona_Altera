#include "tool_common.hpp"

namespace dune3d {

class ConstraintPointOnBezier;

class ToolConvertPointOnBezierConstraint : public ToolCommon {
public:
    using ToolCommon::ToolCommon;

    ToolResponse begin(const ToolArgs &args) override;
    ToolResponse update(const ToolArgs &args) override;
    bool is_specific() override
    {
        return true;
    }

    CanBegin can_begin() override;

private:
    ConstraintPointOnBezier *get_constraint();
};

} // namespace dune3d
