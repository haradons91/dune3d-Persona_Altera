#include "tool_sketch_chamfer_advanced.hpp"
#include "document/document.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "editor/editor_interface.hpp"
#include "dialogs/dialogs.hpp"
#include "dialogs/enter_datum_window.hpp"
#include "core/tool_id.hpp"
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

bool ToolSketchChamferAdvanced::setup_corner()
{
    const std::array<glm::dvec2, 2> line1_points = {m_line1->m_p1, m_line1->m_p2};
    const std::array<glm::dvec2, 2> line2_points = {m_line2->m_p1, m_line2->m_p2};
    double best_distance = 1e-4;
    int corner1 = -1, corner2 = -1;
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            const auto distance = glm::length(line1_points[i] - line2_points[j]);
            if (distance <= best_distance) {
                best_distance = distance;
                corner1 = i;
                corner2 = j;
            }
        }
    }
    if (corner1 < 0)
        return false;
    m_corner = (line1_points[corner1] + line2_points[corner2]) / 2.;
    m_line1_corner = corner1;
    m_line2_corner = corner2;
    const auto line1_end = line1_points[1 - corner1];
    const auto line2_end = line2_points[1 - corner2];
    const auto line1_len = glm::length(line1_end - m_corner);
    const auto line2_len = glm::length(line2_end - m_corner);
    if (line1_len < 1e-6 || line2_len < 1e-6)
        return false;
    m_line1_dir = (line1_end - m_corner) / line1_len;
    m_line2_dir = (line2_end - m_corner) / line2_len;
    return true;
}

ToolBase::CanBegin ToolSketchChamferAdvanced::can_begin()
{
    if (get_workplane_uuid() == UUID())
        return false;
    auto lines = selected_lines(m_selection, get_workplane_uuid(), get_doc());
    if (!lines[0] || !lines[1])
        return false;
    m_line1 = lines[0];
    m_line2 = lines[1];
    return setup_corner();
}

ToolResponse ToolSketchChamferAdvanced::begin(const ToolArgs &args)
{
    auto lines = selected_lines(m_selection, get_workplane_uuid(), get_doc());
    m_line1 = lines[0];
    m_line2 = lines[1];
    if (!m_line1 || !m_line2 || !setup_corner())
        return ToolResponse::end();

    m_intf.get_dialogs().show_enter_datum_window("Enter first distance", DatumUnit::MM, 1.0);
    m_intf.set_no_canvas_update(true);
    m_intf.canvas_update_from_tool();

    return ToolResponse();
}

void ToolSketchChamferAdvanced::commit_chamfer(double first_value, double second_value)
{
    const auto d1 = std::max(first_value, 1e-3);
    glm::dvec2 point2;
    if (m_tool_id == ToolID::SKETCH_CHAMFER_DISTANCE_ANGLE) {
        const auto point1 = m_corner + m_line1_dir * d1;
        const auto angle = second_value * M_PI / 180.0;
        const auto rot = M_PI - angle;
        const glm::dvec2 chamfer_dir(m_line1_dir.x * std::cos(rot) - m_line1_dir.y * std::sin(rot),
                                     m_line1_dir.x * std::sin(rot) + m_line1_dir.y * std::cos(rot));
        const auto inter = line_line_intersect(point1, chamfer_dir, m_corner, m_line2_dir);
        point2 = inter.value_or(m_corner + m_line2_dir * d1);
    }
    else {
        point2 = m_corner + m_line2_dir * std::max(second_value, 1e-3);
    }
    const auto point1 = m_corner + m_line1_dir * d1;

    if (m_line1_corner == 0)
        m_line1->m_p1 = point1;
    else
        m_line1->m_p2 = point1;
    if (m_line2_corner == 0)
        m_line2->m_p1 = point2;
    else
        m_line2->m_p2 = point2;

    auto &new_line = add_entity<EntityLine2D>();
    new_line.m_wrkpl = m_line1->m_wrkpl;
    new_line.m_p1 = point1;
    new_line.m_p2 = point2;

    const EntityAndPoint line1_point{m_line1->m_uuid, static_cast<unsigned int>(m_line1_corner + 1)};
    const EntityAndPoint line2_point{m_line2->m_uuid, static_cast<unsigned int>(m_line2_corner + 1)};

    auto &coincident1 = add_constraint<ConstraintPointsCoincident>();
    coincident1.m_wrkpl = m_line1->m_wrkpl;
    coincident1.m_entity1 = line1_point;
    coincident1.m_entity2 = {new_line.m_uuid, 1};

    auto &coincident2 = add_constraint<ConstraintPointsCoincident>();
    coincident2.m_wrkpl = m_line2->m_wrkpl;
    coincident2.m_entity1 = line2_point;
    coincident2.m_entity2 = {new_line.m_uuid, 2};

    set_current_group_solve_pending();
}

ToolResponse ToolSketchChamferAdvanced::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::DATA) {
        if (auto data = dynamic_cast<const ToolDataWindow *>(args.data.get())) {
            if (data->event == ToolDataWindow::Event::OK) {
                if (auto d = dynamic_cast<const ToolDataEnterDatumWindow *>(args.data.get())) {
                    if (m_stage == Stage::FIRST) {
                        m_first_value = d->value;
                        m_stage = Stage::SECOND;
                        const bool is_angle = m_tool_id == ToolID::SKETCH_CHAMFER_DISTANCE_ANGLE;
                        m_intf.get_dialogs().show_enter_datum_window(
                                is_angle ? "Enter angle" : "Enter second distance",
                                is_angle ? DatumUnit::DEGREE : DatumUnit::MM, is_angle ? 45.0 : 1.0);
                        return ToolResponse();
                    }
                    else {
                        commit_chamfer(m_first_value, d->value);
                        return ToolResponse::commit();
                    }
                }
            }
            else if (data->event == ToolDataWindow::Event::CLOSE) {
                return ToolResponse::revert();
            }
        }
    }
    return ToolResponse();
}

} // namespace dune3d
