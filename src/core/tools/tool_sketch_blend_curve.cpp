#include "tool_sketch_blend_curve.hpp"
#include "document/document.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_bezier2d.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "document/constraint/constraint_bezier_line_tangent.hpp"
#include "editor/editor_interface.hpp"
#include "dialogs/dialogs.hpp"
#include "dialogs/enter_datum_window.hpp"
#include "tool_common_impl.hpp"

#include <array>

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

ToolBase::CanBegin ToolSketchBlendCurve::can_begin()
{
    if (get_workplane_uuid() == UUID())
        return false;
    auto lines = selected_lines(m_selection, get_workplane_uuid(), get_doc());
    return lines[0] && lines[1];
}

ToolResponse ToolSketchBlendCurve::begin(const ToolArgs &args)
{
    auto lines = selected_lines(m_selection, get_workplane_uuid(), get_doc());
    m_line1 = lines[0];
    m_line2 = lines[1];
    if (!m_line1 || !m_line2)
        return ToolResponse::end();

    const std::array<glm::dvec2, 2> line1_points = {m_line1->m_p1, m_line1->m_p2};
    const std::array<glm::dvec2, 2> line2_points = {m_line2->m_p1, m_line2->m_p2};
    double best = 1e18;
    int bi = 0, bj = 0;
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            const auto d = glm::length(line1_points[i] - line2_points[j]);
            if (d < best) {
                best = d;
                bi = i;
                bj = j;
            }
        }
    }
    m_p1 = line1_points[bi];
    m_p2 = line2_points[bj];
    const auto line1_len = glm::length(line1_points[bi] - line1_points[1 - bi]);
    const auto line2_len = glm::length(line2_points[bj] - line2_points[1 - bj]);
    if (line1_len < 1e-6 || line2_len < 1e-6)
        return ToolResponse::end();
    m_line1_dir = (line1_points[bi] - line1_points[1 - bi]) / line1_len;
    m_line2_dir = (line2_points[bj] - line2_points[1 - bj]) / line2_len;
    m_chord = std::max(best, 1e-3);

    m_preview = &add_entity<EntityBezier2D>();
    m_preview->m_wrkpl = m_line1->m_wrkpl;
    m_preview->m_selection_invisible = true;
    apply_bulge(0.5);

    m_intf.get_dialogs().show_enter_datum_window("Enter bulge", DatumUnit::RATIO, 0.5);
    m_intf.set_no_canvas_update(true);
    m_intf.canvas_update_from_tool();

    return ToolResponse();
}

void ToolSketchBlendCurve::apply_bulge(double bulge)
{
    m_preview->m_p1 = m_p1;
    m_preview->m_p2 = m_p2;
    m_preview->m_c1 = m_p1 + m_line1_dir * m_chord * bulge;
    m_preview->m_c2 = m_p2 - m_line2_dir * m_chord * bulge;
}

ToolResponse ToolSketchBlendCurve::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::DATA) {
        if (auto data = dynamic_cast<const ToolDataWindow *>(args.data.get())) {
            if (data->event == ToolDataWindow::Event::UPDATE) {
                if (auto d = dynamic_cast<const ToolDataEnterDatumWindow *>(args.data.get())) {
                    apply_bulge(d->value);
                    set_current_group_solve_pending();
                    m_core.solve_current();
                    set_first_update_group_current();
                    m_intf.canvas_update_from_tool();
                }
            }
            else if (data->event == ToolDataWindow::Event::OK) {
                m_preview->m_selection_invisible = false;

                auto &coincident1 = add_constraint<ConstraintPointsCoincident>();
                coincident1.m_wrkpl = m_line1->m_wrkpl;
                coincident1.m_entity1 = {m_preview->m_uuid, 1};
                coincident1.m_entity2 = {m_line1->m_uuid,
                                          m_p1 == m_line1->m_p1 ? 1u : 2u};

                auto &coincident2 = add_constraint<ConstraintPointsCoincident>();
                coincident2.m_wrkpl = m_line2->m_wrkpl;
                coincident2.m_entity1 = {m_preview->m_uuid, 2};
                coincident2.m_entity2 = {m_line2->m_uuid,
                                          m_p2 == m_line2->m_p1 ? 1u : 2u};

                auto &tangent1 = add_constraint<ConstraintBezierLineTangent>();
                tangent1.m_bezier = {m_preview->m_uuid, 1};
                tangent1.m_line = m_line1->m_uuid;

                auto &tangent2 = add_constraint<ConstraintBezierLineTangent>();
                tangent2.m_bezier = {m_preview->m_uuid, 2};
                tangent2.m_line = m_line2->m_uuid;

                return ToolResponse::commit();
            }
            else if (data->event == ToolDataWindow::Event::CLOSE) {
                return ToolResponse::revert();
            }
        }
    }
    return ToolResponse();
}

} // namespace dune3d
