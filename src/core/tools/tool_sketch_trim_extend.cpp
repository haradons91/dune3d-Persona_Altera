#include "tool_sketch_trim_extend.hpp"
#include "document/document.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "tool_common_impl.hpp"

#include <array>
#include <cmath>
#include <optional>

namespace dune3d {

static std::array<EntityLine2D *, 2> selected_lines(const std::set<SelectableRef> &sel, const UUID &wrkpl,
                                                      Document &doc)
{
    std::array<EntityLine2D *, 2> lines{nullptr, nullptr};
    for (const auto &sr : sel) {
        if (sr.type != SelectableRef::Type::ENTITY)
            continue;
        auto *line = dynamic_cast<EntityLine2D *>(&doc.get_entity(sr.item));
        if (!line || line->m_wrkpl != wrkpl)
            continue;
        if (!lines[0])
            lines[0] = line;
        else if (line != lines[0]) {
            lines[1] = line;
            break;
        }
    }
    return lines;
}

static std::optional<glm::dvec2> line_line_intersect(glm::dvec2 p1, glm::dvec2 d1, glm::dvec2 p2, glm::dvec2 d2)
{
    const double denom = d1.x * d2.y - d1.y * d2.x;
    if (std::abs(denom) < 1e-9)
        return std::nullopt;
    const double t = ((p2.x - p1.x) * d2.y - (p2.y - p1.y) * d2.x) / denom;
    return p1 + d1 * t;
}

ToolBase::CanBegin ToolSketchTrimExtend::can_begin()
{
    if (get_workplane_uuid() == UUID())
        return false;
    auto lines = selected_lines(m_selection, get_workplane_uuid(), get_doc());
    if (!lines[0] || !lines[1])
        return false;
    const auto d1 = lines[0]->m_p2 - lines[0]->m_p1;
    const auto d2 = lines[1]->m_p2 - lines[1]->m_p1;
    return line_line_intersect(lines[0]->m_p1, d1, lines[1]->m_p1, d2).has_value();
}

ToolResponse ToolSketchTrimExtend::begin(const ToolArgs &args)
{
    auto lines = selected_lines(m_selection, get_workplane_uuid(), get_doc());
    auto *line1 = lines[0];
    auto *line2 = lines[1];
    if (!line1 || !line2)
        return ToolResponse::end();

    const auto d1 = line1->m_p2 - line1->m_p1;
    const auto d2 = line2->m_p2 - line2->m_p1;
    const auto inter = line_line_intersect(line1->m_p1, d1, line2->m_p1, d2);
    if (!inter)
        return ToolResponse::end();

    auto move_nearer_endpoint = [&](EntityLine2D &line) -> unsigned int {
        const auto d_p1 = glm::length(line.m_p1 - *inter);
        const auto d_p2 = glm::length(line.m_p2 - *inter);
        if (d_p1 <= d_p2) {
            line.m_p1 = *inter;
            return 1;
        }
        else {
            line.m_p2 = *inter;
            return 2;
        }
    };

    const auto point1 = move_nearer_endpoint(*line1);
    const auto point2 = move_nearer_endpoint(*line2);

    auto &coincident = add_constraint<ConstraintPointsCoincident>();
    coincident.m_wrkpl = line1->m_wrkpl;
    coincident.m_entity1 = {line1->m_uuid, point1};
    coincident.m_entity2 = {line2->m_uuid, point2};

    set_current_group_solve_pending();

    return ToolResponse::commit();
}

ToolResponse ToolSketchTrimExtend::update(const ToolArgs &args)
{
    return ToolResponse();
}

} // namespace dune3d
