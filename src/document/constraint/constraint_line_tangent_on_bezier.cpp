#include "constraint_line_tangent_on_bezier.hpp"
#include "constraint_visitor.hpp"

namespace dune3d {
std::unique_ptr<Constraint> ConstraintLineTangentOnBezier::clone() const
{
    return std::make_unique<ConstraintLineTangentOnBezier>(*this);
}

void ConstraintLineTangentOnBezier::accept(ConstraintVisitor &visitor) const
{
    visitor.visit(*this);
}

} // namespace dune3d
