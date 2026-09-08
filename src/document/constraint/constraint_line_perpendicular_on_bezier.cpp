#include "constraint_line_perpendicular_on_bezier.hpp"
#include "constraint_visitor.hpp"

namespace dune3d {
std::unique_ptr<Constraint> ConstraintLinePerpendicularOnBezier::clone() const
{
    return std::make_unique<ConstraintLinePerpendicularOnBezier>(*this);
}

void ConstraintLinePerpendicularOnBezier::accept(ConstraintVisitor &visitor) const
{
    visitor.visit(*this);
}

} // namespace dune3d
