#include "tool_convert_point_on_bezier_constraint.hpp"
#include "document/document.hpp"
#include "document/entity/entity.hpp"
#include "document/constraint/constraint_line_perpendicular_on_bezier.hpp"
#include "document/constraint/constraint_line_tangent_on_bezier.hpp"

#include "util/selection_util.hpp"
#include "tool_common_impl.hpp"
#include "core/tool_id.hpp"

namespace dune3d {

ConstraintPointOnBezier *ToolConvertPointOnBezierConstraint::get_constraint()
{
    auto filtered = filter_selection(m_selection, SelectableRef::Type::CONSTRAINT);
    if (filtered.size() != 1)
        return nullptr;

    auto &co = get_doc().get_constraint(filtered.begin()->item);
    return dynamic_cast<ConstraintPointOnBezier *>(&co);
}

ToolBase::CanBegin ToolConvertPointOnBezierConstraint::can_begin()
{
    auto constraint = get_constraint();
    if (!constraint)
        return false;

    const auto &en_point = get_entity(constraint->m_point.entity);
    using CT = Constraint::Type;
    using ET = Entity::Type;
    const auto is_line = en_point.of_type(ET::LINE_2D);

    switch (m_tool_id) {
    case ToolID::CONVERT_TO_LINE_PERPENDICULAR_ON_BEZIER_CONSTRAINT:
        if (constraint->of_type(CT::LINE_PERDENDICULAR_ON_BEZIER))
            return false;
        return is_line;

    case ToolID::CONVERT_TO_LINE_TANGENT_ON_BEZIER_CONSTRAINT:
        if (constraint->of_type(CT::LINE_TANGENT_ON_BEZIER))
            return false;
        return is_line;

    case ToolID::CONVERT_TO_POINT_ON_BEZIER_CONSTRAINT:
        return !constraint->of_type(CT::POINT_ON_BEZIER);

    default:
        return false;
    }

    return false;
}

ToolResponse ToolConvertPointOnBezierConstraint::begin(const ToolArgs &args)
{
    auto constraint = get_constraint();
    if (!constraint)
        return ToolResponse::end();

    ConstraintPointOnBezier *new_constraint = nullptr;

    switch (m_tool_id) {
    case ToolID::CONVERT_TO_LINE_PERPENDICULAR_ON_BEZIER_CONSTRAINT:
        new_constraint = &add_constraint<ConstraintLinePerpendicularOnBezier>();
        break;

    case ToolID::CONVERT_TO_LINE_TANGENT_ON_BEZIER_CONSTRAINT:
        new_constraint = &add_constraint<ConstraintLineTangentOnBezier>();
        break;

    case ToolID::CONVERT_TO_POINT_ON_BEZIER_CONSTRAINT:
        new_constraint = &add_constraint<ConstraintPointOnBezier>();
        break;

    default:
        return ToolResponse::end();
    }

    new_constraint->m_line = constraint->m_line;
    new_constraint->m_point = constraint->m_point;
    new_constraint->m_val = constraint->m_val;
    new_constraint->m_wrkpl = constraint->m_wrkpl;

    get_doc().delete_items({.constraints = {constraint->m_uuid}});

    return ToolResponse::commit();
}

ToolResponse ToolConvertPointOnBezierConstraint::update(const ToolArgs &args)
{

    return ToolResponse();
}

} // namespace dune3d
