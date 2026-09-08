#pragma once
#include "constraint_point_on_bezier.hpp"

namespace dune3d {
class ConstraintLinePerpendicularOnBezier : public ConstraintPointOnBezier {
public:
    using ConstraintPointOnBezier::ConstraintPointOnBezier;
    static constexpr Type s_type = Type::LINE_PERDENDICULAR_ON_BEZIER;
    Type get_type() const override
    {
        return s_type;
    }
    std::unique_ptr<Constraint> clone() const override;
    void accept(ConstraintVisitor &visitor) const override;
};
} // namespace dune3d
