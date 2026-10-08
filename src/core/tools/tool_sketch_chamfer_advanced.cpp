#include "tool_sketch_chamfer_advanced.hpp"
#include "document/document.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "editor/editor_interface.hpp"
#include "dialogs/rectangle_dimensions_window.hpp"
#include "core/tool_id.hpp"
#include "tool_common_impl.hpp"

#include <algorithm>
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

    m_preview = &add_entity<EntityLine2D>();
    m_preview->m_wrkpl = m_line1->m_wrkpl;
    m_preview->m_construction = true;
    m_preview->m_selection_invisible = true;

    m_dist1 = 1.0;
    m_dist2 = m_tool_id == ToolID::SKETCH_CHAMFER_DISTANCE_ANGLE ? 45.0 : 1.0;
    m_active_is_first = true;

    update_preview();
    m_intf.show_rectangle_dimensions(m_dist1, m_dist2, true, true);
    m_intf.canvas_update_from_tool();

    return ToolResponse();
}

glm::dvec2 ToolSketchChamferAdvanced::point1() const
{
    return m_corner + m_line1_dir * std::max(m_dist1, 1e-3);
}

glm::dvec2 ToolSketchChamferAdvanced::point2() const
{
    const auto d1 = std::max(m_dist1, 1e-3);
    if (m_tool_id == ToolID::SKETCH_CHAMFER_DISTANCE_ANGLE) {
        const auto p1 = m_corner + m_line1_dir * d1;
        const auto angle = std::clamp(m_dist2, 1.0, 179.0) * M_PI / 180.0;
        const auto rot = M_PI - angle;
        const glm::dvec2 chamfer_dir(m_line1_dir.x * std::cos(rot) - m_line1_dir.y * std::sin(rot),
                                     m_line1_dir.x * std::sin(rot) + m_line1_dir.y * std::cos(rot));
        const auto inter = line_line_intersect(p1, chamfer_dir, m_corner, m_line2_dir);
        return inter.value_or(m_corner + m_line2_dir * d1);
    }
    return m_corner + m_line2_dir * std::max(m_dist2, 1e-3);
}

void ToolSketchChamferAdvanced::update_preview()
{
    const auto workplane = get_workplane();
    const auto cursor = workplane->project(get_cursor_pos_for_workplane(*workplane));

    // Only the ACTIVE side tracks the mouse live -- Tab switches which
    // one that is, matching Fillet/Offset's single-value mouse-drag
    // pattern but doubled, one per side of the corner.
    if (m_active_is_first) {
        m_dist1 = std::max(glm::dot(cursor - m_corner, m_line1_dir), 1e-3);
    }
    else if (m_tool_id == ToolID::SKETCH_CHAMFER_DISTANCE_ANGLE) {
        // Invert the point1->chamfer_dir->angle relationship point2()
        // uses: recover the angle that would make the chamfer line pass
        // through the cursor.
        const auto p1 = point1();
        const auto dir = cursor - p1;
        if (glm::length(dir) > 1e-9) {
            const auto chamfer_dir = glm::normalize(dir);
            const auto rot_actual = std::atan2(m_line1_dir.x * chamfer_dir.y - m_line1_dir.y * chamfer_dir.x,
                                                glm::dot(m_line1_dir, chamfer_dir));
            const auto angle_deg = (M_PI - rot_actual) * 180.0 / M_PI;
            m_dist2 = std::clamp(angle_deg, 1.0, 179.0);
        }
    }
    else {
        m_dist2 = std::max(glm::dot(cursor - m_corner, m_line2_dir), 1e-3);
    }

    const auto p1 = point1();
    const auto p2 = point2();
    m_preview->m_p1 = p1;
    m_preview->m_p2 = p2;
    m_preview->m_visible = true;

    m_intf.update_rectangle_dimensions(m_dist1, m_dist2, true, true);
    m_intf.position_rectangle_dimensions(workplane->transform(m_corner), workplane->transform(m_corner),
                                         workplane->transform(p1), workplane->transform(m_corner),
                                         workplane->transform(p2), false, false);
}

void ToolSketchChamferAdvanced::commit_chamfer()
{
    const auto p1 = point1();
    const auto p2 = point2();

    // If the two lines' shared corner came from an existing
    // ConstraintPointsCoincident (e.g. a rectangle's corner), it's still
    // demanding both points stay equal even though the chamfer is about
    // to move them to two DIFFERENT points -- drop it first, or the
    // solver fights that old constraint and just shrinks/drags the rest
    // of the connected shape instead of actually chamfering the corner.
    // Same bug class ToolSketchTrimExtend hit extending a rectangle edge.
    const EntityAndPoint line1_corner_point{m_line1->m_uuid, static_cast<unsigned int>(m_line1_corner + 1)};
    const EntityAndPoint line2_corner_point{m_line2->m_uuid, static_cast<unsigned int>(m_line2_corner + 1)};
    for (auto it = get_doc().m_constraints.begin(); it != get_doc().m_constraints.end();) {
        auto *coincident = dynamic_cast<ConstraintPointsCoincident *>(it->second.get());
        if (coincident
            && ((coincident->m_entity1 == line1_corner_point && coincident->m_entity2 == line2_corner_point)
                || (coincident->m_entity1 == line2_corner_point && coincident->m_entity2 == line1_corner_point)))
            it = get_doc().m_constraints.erase(it);
        else
            ++it;
    }

    if (m_line1_corner == 0)
        m_line1->m_p1 = p1;
    else
        m_line1->m_p2 = p1;
    if (m_line2_corner == 0)
        m_line2->m_p1 = p2;
    else
        m_line2->m_p2 = p2;

    if (m_preview)
        get_doc().m_entities.erase(m_preview->m_uuid);

    auto &new_line = add_entity<EntityLine2D>();
    new_line.m_wrkpl = m_line1->m_wrkpl;
    new_line.m_p1 = p1;
    new_line.m_p2 = p2;

    auto &coincident1 = add_constraint<ConstraintPointsCoincident>();
    coincident1.m_wrkpl = m_line1->m_wrkpl;
    coincident1.m_entity1 = line1_corner_point;
    coincident1.m_entity2 = {new_line.m_uuid, 1};

    auto &coincident2 = add_constraint<ConstraintPointsCoincident>();
    coincident2.m_wrkpl = m_line2->m_wrkpl;
    coincident2.m_entity1 = line2_corner_point;
    coincident2.m_entity2 = {new_line.m_uuid, 2};

    set_current_group_solve_pending();
}

ToolResponse ToolSketchChamferAdvanced::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::MOVE) {
        update_preview();
        set_first_update_group_current();
        return ToolResponse();
    }

    if (args.type == ToolEventType::DATA) {
        if (auto data = dynamic_cast<const ToolDataRectangleDimensionsWindow *>(args.data.get())) {
            if (data->event == ToolDataWindow::Event::UPDATE) {
                // Tab locks whichever side you're leaving at its current
                // value and hands mouse-drag control to the other one.
                if (data->lock_width) {
                    m_dist1 = data->width;
                    m_active_is_first = false;
                }
                if (data->lock_height) {
                    m_dist2 = data->height;
                    m_active_is_first = true;
                }
                update_preview();
                m_intf.canvas_update_from_tool();
            }
        }
        return ToolResponse();
    }

    if (args.type != ToolEventType::ACTION)
        return ToolResponse();

    switch (args.action) {
    case InToolActionID::LMB:
        // First click locks side 1 at its current (dragged) value and
        // hands mouse-drag control to side 2, same as Tab -- second
        // click commits. Tab still works too, this is just a second way
        // to advance without touching the keyboard.
        if (m_active_is_first) {
            m_active_is_first = false;
            update_preview();
            m_intf.canvas_update_from_tool();
            return ToolResponse();
        }
        m_intf.hide_rectangle_dimensions();
        commit_chamfer();
        return ToolResponse::commit();

    case InToolActionID::RMB:
    case InToolActionID::CANCEL:
        m_intf.hide_rectangle_dimensions();
        if (m_preview)
            get_doc().m_entities.erase(m_preview->m_uuid);
        return ToolResponse::revert();

    default:
        return ToolResponse();
    }
}

} // namespace dune3d
