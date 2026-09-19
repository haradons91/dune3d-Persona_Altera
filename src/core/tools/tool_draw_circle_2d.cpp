#include "tool_draw_circle_2d.hpp"
#include "core/tool_id.hpp"
#include "document/document.hpp"
#include "document/entity/entity_circle2d.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "document/constraint/constraint_point_on_line.hpp"
#include "document/constraint/constraint_point_on_circle.hpp"
#include "editor/editor_interface.hpp"
#include "dialogs/rectangle_dimensions_window.hpp"
#include "util/selection_util.hpp"
#include "util/action_label.hpp"
#include "tool_common_impl.hpp"

namespace dune3d {

ToolResponse ToolDrawCircle2D::begin(const ToolArgs &args)
{
    m_wrkpl = get_workplane();
    m_intf.enable_hover_selection();
    return ToolResponse();
}

ToolBase::CanBegin ToolDrawCircle2D::can_begin()
{
    return get_workplane_uuid() != UUID();
}

glm::dvec2 ToolDrawCircle2D::get_cursor_pos_in_plane() const
{
    return m_wrkpl->project(get_cursor_pos_for_workplane(*m_wrkpl));
}

bool ToolDrawCircle2D::update_three_point_circle(const glm::dvec2 &third_point)
{
    const auto a = m_first_point;
    const auto b = m_second_point;
    const auto d = 2. * (a.x * (b.y - third_point.y) + b.x * (third_point.y - a.y)
                          + third_point.x * (a.y - b.y));
    if (std::abs(d) <= 1e-12) {
        m_temp_circle->m_radius = 0;
        return false;
    }

    m_temp_circle->m_center = {
            (glm::dot(a, a) * (b.y - third_point.y) + glm::dot(b, b) * (third_point.y - a.y)
             + glm::dot(third_point, third_point) * (a.y - b.y))
                    / d,
            (glm::dot(a, a) * (third_point.x - b.x) + glm::dot(b, b) * (a.x - third_point.x)
             + glm::dot(third_point, third_point) * (b.x - a.x))
                    / d};
    m_temp_circle->m_radius = glm::length(third_point - m_temp_circle->m_center);
    return true;
}

ToolResponse ToolDrawCircle2D::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::MOVE) {
        if (m_temp_circle) {
            const auto p = get_cursor_pos_in_plane();
            if (!m_diameter_locked && m_tool_id == ToolID::DRAW_CIRCLE_2_POINT && m_points_placed == 1) {
                m_temp_circle->m_center = (m_first_point + p) / 2.;
                m_temp_circle->m_radius = glm::length(p - m_first_point) / 2.;
            }
            else if (!m_diameter_locked && m_tool_id == ToolID::DRAW_CIRCLE_3_POINT && m_points_placed == 2) {
                update_three_point_circle(p);
            }
            else if (!m_diameter_locked)
                m_temp_circle->m_radius = glm::length(p - m_temp_circle->m_center);

            const auto center = m_wrkpl->transform(m_temp_circle->m_center);
            const auto radius = m_wrkpl->transform({m_temp_circle->m_center.x + m_temp_circle->m_radius,
                                                     m_temp_circle->m_center.y});
            const auto diameter = 2. * m_temp_circle->m_radius;
            m_intf.update_circle_dimension(diameter);
            m_intf.position_circle_dimension(center,
                                             m_wrkpl->transform({m_temp_circle->m_center.x - m_temp_circle->m_radius,
                                                                 m_temp_circle->m_center.y}),
                                             radius);
        }
        update_tip();
        set_first_update_group_current();
        return ToolResponse();
    }
    else if (args.type == ToolEventType::ACTION) {
        switch (args.action) {
        case InToolActionID::LMB: {
            if (m_temp_circle && m_tool_id == ToolID::DRAW_CIRCLE_3_POINT && m_points_placed == 1) {
                m_second_point = get_cursor_pos_in_plane();
                m_points_placed = 2;
                return ToolResponse();
            }
            else if (m_temp_circle) {
                if (m_tool_id == ToolID::DRAW_CIRCLE_2_POINT && m_points_placed == 1) {
                    const auto p = get_cursor_pos_in_plane();
                    m_temp_circle->m_center = (m_first_point + p) / 2.;
                    if (!m_diameter_locked)
                        m_temp_circle->m_radius = glm::length(p - m_first_point) / 2.;
                }
                else if (m_tool_id == ToolID::DRAW_CIRCLE_3_POINT && m_points_placed == 2) {
                    const auto p = get_cursor_pos_in_plane();
                    if (!m_diameter_locked && !update_three_point_circle(p))
                        return ToolResponse();
                }
                m_temp_circle->m_selection_invisible = false;
                m_intf.hide_circle_dimension();
                if (m_constrain) {
                    if (auto hsel = m_intf.get_hover_selection()) {
                        if (hsel->type == SelectableRef::Type::ENTITY) {
                            const auto enp = hsel->get_entity_and_point();
                            if (get_doc().is_valid_point(enp)) {
                                auto &constraint = add_constraint<ConstraintPointOnCircle>();
                                constraint.m_circle = m_temp_circle->m_uuid;
                                constraint.m_point = enp;
                                constraint.m_modify_to_satisfy = true;
                            }
                        }
                    }
                }
                return ToolResponse::commit();
            }
            else {
                m_temp_circle = &add_entity<EntityCircle2D>();
                m_temp_circle->m_selection_invisible = true;
                m_temp_circle->m_radius = 0;
                m_temp_circle->m_center = get_cursor_pos_in_plane();
                m_temp_circle->m_wrkpl = m_wrkpl->m_uuid;
                m_first_point = m_temp_circle->m_center;
                m_points_placed = 1;
                m_diameter_locked = false;
                m_intf.show_circle_dimension(0);

                if (m_constrain && m_tool_id == ToolID::DRAW_CIRCLE_2D) {
                    const EntityAndPoint circle_center{m_temp_circle->m_uuid, 1};
                    constrain_point(m_wrkpl->m_uuid, circle_center);
                }

                return ToolResponse();
            }
        } break;

        case InToolActionID::TOGGLE_CONSTRUCTION: {
            if (m_temp_circle)
                m_temp_circle->m_construction = !m_temp_circle->m_construction;
        } break;

        case InToolActionID::TOGGLE_COINCIDENT_CONSTRAINT: {
            m_constrain = !m_constrain;
        } break;

        case InToolActionID::RMB:
        case InToolActionID::CANCEL:
            m_intf.hide_circle_dimension();
            return ToolResponse::revert();

        default:;
        }
        update_tip();
    }

    else if (args.type == ToolEventType::DATA) {
        if (auto data = dynamic_cast<const ToolDataCircleDimensionsWindow *>(args.data.get())) {
            if (data->event == ToolDataWindow::Event::UPDATE && m_temp_circle) {
                m_temp_circle->m_radius = std::abs(data->diameter) / 2.;
                m_diameter_locked = true;
                set_first_update_group_current();
                m_intf.canvas_update_from_tool();
            }
        }
    }

    return ToolResponse();
}

void ToolDrawCircle2D::update_tip()
{
    std::vector<ActionLabelInfo> actions;

    if (m_temp_circle) {
        if (m_tool_id == ToolID::DRAW_CIRCLE_3_POINT) {
            if (m_points_placed == 1)
                actions.emplace_back(InToolActionID::LMB, "place second point");
            else
                actions.emplace_back(InToolActionID::LMB, "place third point");
        }
        else
            actions.emplace_back(InToolActionID::LMB, "place radius");
    }
    else
        actions.emplace_back(InToolActionID::LMB, "place center");

    actions.emplace_back(InToolActionID::RMB, "end tool");

    if (m_temp_circle) {
        if (m_temp_circle->m_construction)
            actions.emplace_back(InToolActionID::TOGGLE_CONSTRUCTION, "normal");
        else
            actions.emplace_back(InToolActionID::TOGGLE_CONSTRUCTION, "construction");
    }

    if (m_constrain)
        actions.emplace_back(InToolActionID::TOGGLE_COINCIDENT_CONSTRAINT, "constraint off");
    else
        actions.emplace_back(InToolActionID::TOGGLE_COINCIDENT_CONSTRAINT, "constraint on");


    std::vector<ConstraintType> constraint_icons;
    glm::vec3 v = {NAN, NAN, NAN};

    m_intf.tool_bar_set_tool_tip("");
    if (m_constrain && !m_temp_circle) {
        set_constrain_tip("center");
        update_constraint_icons(constraint_icons);
    }
    if (m_constrain && m_temp_circle) {
        if (auto hsel = m_intf.get_hover_selection()) {
            if (hsel->type == SelectableRef::Type::ENTITY) {
                const auto enp = hsel->get_entity_and_point();
                if (get_doc().is_valid_point(enp)) {
                    const auto r = get_cursor_pos_in_plane() - m_temp_circle->m_center;
                    v = m_wrkpl->transform_relative({-r.y, r.x});
                    constraint_icons.push_back(Constraint::Type::POINT_ON_CIRCLE);
                    m_intf.tool_bar_set_tool_tip("constrain radius on point");
                }
            }
        }
    }

    m_intf.set_constraint_icons(get_cursor_pos_for_workplane(*m_wrkpl), v, constraint_icons);

    m_intf.tool_bar_set_actions(actions);
}
} // namespace dune3d
