#include "tool_constrain_distance.hpp"
#include "document/document.hpp"
#include "document/constraint/constraint_point_distance.hpp"
#include "document/constraint/constraint_point_distance_hv.hpp"
#include "document/constraint/constraint_point_line_distance.hpp"
#include "document/constraint/constraint_diameter_radius.hpp"
#include "document/entity/ientity_in_workplane.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/constraint/constraint_points_coincident.hpp"
#include "util/selection_util.hpp"
#include "util/template_util.hpp"
#include "core/tool_id.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include "tool_common_constrain_impl.hpp"
#include <fstream>
#include <format>
#include <array>
#include <algorithm>
#include <limits>
#include <vector>

namespace dune3d {
namespace {
void sketch_dimension_debug_log(const std::string &message)
{
    static std::ofstream log("/tmp/dune3d-sketch-dimension-debug.log", std::ios::app);
    log << message << '\n';
    log.flush();
}

std::string describe_selection(const std::set<SelectableRef> &selection)
{
    std::string result;
    for (const auto &sr : selection) {
        if (!result.empty())
            result += ",";
        result += std::format("type={} point={} item={}", static_cast<int>(sr.type), sr.point,
                              static_cast<std::string>(sr.item));
    }
    return result.empty() ? "<empty>" : result;
}

std::optional<LineAndPoint> line_and_line_from_selection(const Document &doc,
                                                         const std::set<SelectableRef> &selection)
{
    const auto entities = entities_from_selection(selection);
    if (entities.size() != 2)
        return {};
    auto it = entities.begin();
    const auto first = *it++;
    const auto second = *it;
    if (first.point != 0 || second.point != 0)
        return {};
    const auto *first_line = dynamic_cast<const EntityLine2D *>(&doc.get_entity(first.item));
    const auto *second_line = dynamic_cast<const EntityLine2D *>(&doc.get_entity(second.item));
    if (!first_line || !second_line || first.item == second.item || first_line->m_wrkpl != second_line->m_wrkpl)
        return {};
    // A line-to-line dimension is represented by the first line's endpoint
    // measured to the second line.  The solver then keeps the selected
    // rectangle sides at the requested spacing instead of making a line-length
    // constraint on either side.
    return LineAndPoint{second.item, EntityAndPoint{first.item, 1}};
}

std::optional<std::pair<EntityAndPoint, EntityAndPoint>> intersecting_rectangle_lines(const Document &doc,
                                                                                        const UUID &line_uuid)
{
    const auto find_connection = [&](const EntityAndPoint &point, const UUID &exclude) -> std::optional<EntityAndPoint> {
        for (const auto &[uuid, constraint] : doc.m_constraints) {
            const auto *coincident = dynamic_cast<const ConstraintPointsCoincident *>(constraint.get());
            if (!coincident)
                continue;
            EntityAndPoint other;
            if (coincident->m_entity1 == point)
                other = coincident->m_entity2;
            else if (coincident->m_entity2 == point)
                other = coincident->m_entity1;
            else
                continue;
            if (other.entity != exclude)
                return other;
        }
        return {};
    };

    const auto resolve_straight_line = [&](EntityAndPoint connection) -> std::optional<EntityAndPoint> {
        if (dynamic_cast<const EntityLine2D *>(&doc.get_entity(connection.entity)))
            return connection;

        // A fillet replaces a rectangle corner with an arc. Follow the arc
        // through its other endpoint to recover the straight boundary line.
        const auto &entity = doc.get_entity(connection.entity);
        if (!entity.is_valid_point(connection.point))
            return {};
        const auto other_point = connection.point == 1 ? 2u : 1u;
        auto other = find_connection({connection.entity, other_point}, connection.entity);
        if (!other)
            return {};
        if (dynamic_cast<const EntityLine2D *>(&doc.get_entity(other->entity)))
            return other;
        return {};
    };

    const auto *selected = dynamic_cast<const EntityLine2D *>(&doc.get_entity(line_uuid));
    if (!selected)
        return {};

    std::array<std::optional<EntityAndPoint>, 2> adjacent;
    for (unsigned int point = 1; point <= 2; point++)
        adjacent.at(point - 1) = find_connection({line_uuid, point}, line_uuid);
    if (adjacent[0] && adjacent[1] && adjacent[0]->entity != adjacent[1]->entity) {
        auto line1 = resolve_straight_line(*adjacent[0]);
        auto line2 = resolve_straight_line(*adjacent[1]);
        if (selected && line1 && line2 && line1->entity != line2->entity) {
            const auto *line1_entity = dynamic_cast<const EntityLine2D *>(&doc.get_entity(line1->entity));
            const auto *line2_entity = dynamic_cast<const EntityLine2D *>(&doc.get_entity(line2->entity));
            if (line1_entity && line2_entity) {
                const auto line1_direction = line1_entity->m_p2 - line1_entity->m_p1;
                const auto line2_direction = line2_entity->m_p2 - line2_entity->m_p1;
                if (std::abs(line1_direction.x * line2_direction.y
                             - line1_direction.y * line2_direction.x)
                    <= 1e-6) {
                sketch_dimension_debug_log("line span uses the two intersecting rectangle lines");
                return std::make_pair(*line1, *line2);
                }
            }
        }
    }

    // After several fillets, the coincidence constraints can be replaced or
    // removed. Use the two closest straight edges perpendicular to the
    // selected edge as a geometric fallback.
    const auto selected_direction = selected->m_p2 - selected->m_p1;
    const auto selected_length = glm::length(selected_direction);
    if (selected_length > 1e-9) {
        struct Candidate {
            UUID uuid;
            unsigned int point;
            double distance;
        };
        const auto candidates_near = [&](const glm::dvec2 &target) {
            std::vector<Candidate> candidates;
            for (const auto &[uuid, entity] : doc.m_entities) {
            const auto *candidate = dynamic_cast<const EntityLine2D *>(entity.get());
                if (!candidate || candidate->m_construction || uuid == line_uuid
                    || candidate->m_wrkpl != selected->m_wrkpl)
                    continue;
                const auto direction = candidate->m_p2 - candidate->m_p1;
                const auto length = glm::length(direction);
                if (length <= 1e-9)
                    continue;
                const auto dot = std::abs(glm::dot(selected_direction, direction) / (selected_length * length));
                if (dot > 1e-6)
                    continue;
                const auto distance1 = glm::length(candidate->m_p1 - target);
                const auto distance2 = glm::length(candidate->m_p2 - target);
                candidates.push_back({uuid, distance1 < distance2 ? 1u : 2u, std::min(distance1, distance2)});
            }
            std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
                return a.distance < b.distance;
            });
            return candidates;
        };

        const auto first_candidates = candidates_near(selected->m_p1);
        const auto second_candidates = candidates_near(selected->m_p2);
        if (!first_candidates.empty() && !second_candidates.empty()) {
            const auto *first = &first_candidates.front();
            const auto *second = &second_candidates.front();
            if (first->uuid == second->uuid) {
                if (second_candidates.size() < 2)
                    return {};
                second = &second_candidates[1];
            }
            sketch_dimension_debug_log("line span uses geometric perpendicular-line fallback");
            return std::make_pair(EntityAndPoint{first->uuid, first->point},
                                  EntityAndPoint{second->uuid, second->point});
        }
    }

    sketch_dimension_debug_log("line span intersecting lines not found");
    return {};
}
} // namespace

ToolBase::CanBegin ToolConstrainDistance::can_begin()
{
    sketch_dimension_debug_log(std::format("can_begin selection_count={} tool_id={} selection={}", m_selection.size(),
                                           static_cast<int>(m_tool_id), describe_selection(m_selection)));
    // Allow the Sketch Dimension command to be activated before a line is
    // selected. The first canvas click will provide the line selection.
    if (m_tool_id == ToolID::CONSTRAIN_DISTANCE && m_selection.empty())
        return true;

    // A first endpoint may be selected while the dimension tool is active;
    // wait for the second endpoint before creating the constraint.
    if (m_tool_id == ToolID::CONSTRAIN_DISTANCE && m_selection.size() == 1) {
        const auto &sr = *m_selection.begin();
        if (sr.type == SelectableRef::Type::ENTITY && sr.point == 0) {
            auto &entity = get_doc().get_entity(sr.item);
            if (entity.of_type(Entity::Type::ARC_2D, Entity::Type::CIRCLE_2D)) {
                if (!any_entity_from_current_group(entity))
                    return false;
                const auto types = entity.get_constraint_types(get_doc());
                return !set_contains(types, Constraint::Type::RADIUS)
                       && !set_contains(types, Constraint::Type::DIAMETER);
            }
        }
        if (sr.type == SelectableRef::Type::ENTITY && sr.point != 0
            && get_doc().get_entity(sr.item).is_valid_point(sr.point))
            return true;
    }

    if (m_tool_id == ToolID::CONSTRAIN_DISTANCE) {
        if (auto lp = line_and_line_from_selection(get_doc(), m_selection)) {
            if (!any_entity_from_current_group(lp->get_enps()))
                return false;
            return true;
        }
        if (auto lp = line_and_point_from_selection(get_doc(), m_selection, LineAndPoint::AllowSameEntity::NO)) {
            if (!any_entity_from_current_group(lp->get_enps()))
                return false;
            return !has_constraint_of_type_in_workplane(lp->get_enps(), Constraint::Type::POINT_LINE_DISTANCE,
                                                        Constraint::Type::POINT_ON_LINE, Constraint::Type::MIDPOINT);
        }
    }

    if (any_of(m_tool_id, ToolID::CONSTRAIN_DISTANCE_HORIZONTAL, ToolID::CONSTRAIN_DISTANCE_VERTICAL,
               ToolID::MEASURE_DISTANCE_HORIZONTAL, ToolID::MEASURE_DISTANCE_VERTICAL)
        && !get_workplane_uuid())
        return false;

    auto tp = two_points_from_selection(get_doc(), m_selection);
    if (!tp)
        return false;

    sketch_dimension_debug_log(std::format(
            "resolved points entity1_type={} entity1_group={} entity2_type={} entity2_group={} current_group={}",
            static_cast<int>(get_doc().get_entity(tp->point1.entity).get_type()),
            static_cast<std::string>(get_doc().get_entity(tp->point1.entity).m_group),
            static_cast<int>(get_doc().get_entity(tp->point2.entity).get_type()),
            static_cast<std::string>(get_doc().get_entity(tp->point2.entity).m_group),
            static_cast<std::string>(m_core.get_current_group())));

    if (any_of(m_tool_id, ToolID::MEASURE_DISTANCE, ToolID::MEASURE_DISTANCE_HORIZONTAL,
               ToolID::MEASURE_DISTANCE_VERTICAL))
        return true;

    if (!any_entity_from_current_group(tp->get_enps_as_tuple())) {
        sketch_dimension_debug_log("rejected: neither selected endpoint entity is in current group");
        return false;
    }

    switch (m_tool_id) {
    case ToolID::CONSTRAIN_DISTANCE_HORIZONTAL:
        return !has_constraint_of_type_in_workplane(tp->get_enps(), Constraint::Type::POINT_DISTANCE_HORIZONTAL,
                                                    Constraint::Type::VERTICAL, Constraint::Type::SYMMETRIC_VERTICAL);

    case ToolID::CONSTRAIN_DISTANCE_VERTICAL:
        return !has_constraint_of_type_in_workplane(tp->get_enps(), Constraint::Type::POINT_DISTANCE_VERTICAL,
                                                    Constraint::Type::HORIZONTAL,
                                                    Constraint::Type::SYMMETRIC_HORIZONTAL);

    case ToolID::CONSTRAIN_DISTANCE:
    case ToolID::CONSTRAIN_DISTANCE_3D:
        // A line-length dimension is compatible with orientation constraints;
        // only another point-distance dimension is a conflicting duplicate.
        if (has_constraint_of_type_in_workplane(tp->get_enps(), Constraint::Type::POINT_DISTANCE)) {
            sketch_dimension_debug_log("rejected: selected line already has a conflicting dimensional constraint");
            return false;
        }
        return true;

    default:
        return false;
    }
}

ToolResponse ToolConstrainDistance::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::ACTION)
        sketch_dimension_debug_log(std::format("update action={} selection_count={} tool_selection_count={}",
                                               static_cast<int>(args.action), args.selection.size(),
                                               m_selection.size()));

    // Escape must cancel both the pre-selection state and an active datum
    // editor. Handle it before the endpoint-selection guards below, which
    // intentionally swallow motion/click events while waiting for input.
    if (args.type == ToolEventType::ACTION && args.action == InToolActionID::CANCEL)
        return ToolResponse::revert();

    if (m_selection.empty()) {
        if (args.type == ToolEventType::ACTION && args.action == InToolActionID::LMB && !args.selection.empty()) {
            m_selection = args.selection;
            if (args.m_keep_selection && m_selection.size() == 1) {
                sketch_dimension_debug_log("Ctrl-click kept first dimension item selected");
                return ToolResponse();
            }
            if (m_tool_id == ToolID::CONSTRAIN_DISTANCE && m_selection.size() == 1) {
                const auto &sr = *m_selection.begin();
                if (sr.type == SelectableRef::Type::ENTITY && sr.point == 0
                    && dynamic_cast<const EntityLine2D *>(&get_doc().get_entity(sr.item))) {
                    sketch_dimension_debug_log("selected first rectangle side; waiting for second side");
                    return ToolResponse();
                }
            }
            if (two_points_from_selection(get_doc(), m_selection)) {
                if (can_begin().can_begin == CanBegin::NO) {
                    sketch_dimension_debug_log("selection rejected by can_begin");
                    return ToolResponse::end();
                }
                // Endpoint selection waits for one more endpoint. Whole
                // rectangle sides wait for a second side above.
                const auto &sr = *m_selection.begin();
                if (m_selection.size() == 1 && sr.type == SelectableRef::Type::ENTITY && sr.point != 0)
                    return ToolResponse();
                return begin(args);
            }
            if (m_selection.size() == 1) {
                const auto &sr = *m_selection.begin();
                if (sr.type == SelectableRef::Type::ENTITY && sr.point != 0
                    && get_doc().get_entity(sr.item).is_valid_point(sr.point))
                    return ToolResponse();
            }
            else {
                sketch_dimension_debug_log("selection rejected by can_begin");
                return ToolResponse::end();
            }
        }
        // No constraint exists yet, so the datum base class must not receive
        // motion or confirmation events in this pre-selection state.
        return ToolResponse();
    }
    if (m_selection.size() == 1 && args.type == ToolEventType::ACTION && args.action == InToolActionID::LMB
        && args.selection.size() > 1) {
        // The canvas keeps the first endpoint selected and adds the second
        // endpoint on the next click.
        if (two_points_from_selection(get_doc(), args.selection)) {
            m_selection = args.selection;
            if (can_begin().can_begin == CanBegin::NO) {
                sketch_dimension_debug_log("two-point selection rejected by can_begin");
                return ToolResponse::end();
            }
            return begin(args);
        }
        if (m_tool_id == ToolID::CONSTRAIN_DISTANCE
            && line_and_point_from_selection(get_doc(), args.selection, LineAndPoint::AllowSameEntity::NO)) {
            m_selection = args.selection;
            if (can_begin().can_begin == CanBegin::NO) {
                sketch_dimension_debug_log("point-line selection rejected by can_begin");
                return ToolResponse::end();
            }
            return begin(args);
        }
        if (m_tool_id == ToolID::CONSTRAIN_DISTANCE
            && line_and_line_from_selection(get_doc(), args.selection)) {
            m_selection = args.selection;
            if (can_begin().can_begin == CanBegin::NO) {
                sketch_dimension_debug_log("line-to-line selection rejected by can_begin");
                return ToolResponse::end();
            }
            return begin(args);
        }
        return ToolResponse();
    }
    // One endpoint has been selected, but no distance constraint exists yet.
    // Do not pass motion/cancel events to the datum base class, whose update
    // path expects an initialized constraint.
    if (m_selection.size() == 1 && !has_interactive_constraint())
        return ToolResponse();
    return ToolCommonConstrainDatum::update(args);
}

bool ToolConstrainDistance::can_preview_constrain()
{
    return any_of(m_tool_id, ToolID::CONSTRAIN_DISTANCE, ToolID::CONSTRAIN_DISTANCE_3D,
                  ToolID::CONSTRAIN_DISTANCE_HORIZONTAL, ToolID::CONSTRAIN_DISTANCE_VERTICAL);
}

bool ToolConstrainDistance::is_force_unset_workplane()
{
    return m_tool_id == ToolID::CONSTRAIN_DISTANCE_3D;
}

bool ToolConstrainDistance::constraint_is_in_workplane()
{
    return get_workplane_uuid() != UUID{};
}

ToolID ToolConstrainDistance::get_force_unset_workplane_tool()
{
    if (m_tool_id != ToolID::CONSTRAIN_DISTANCE)
        return ToolID::NONE;

    auto wrkpl = get_workplane_uuid();
    if (!wrkpl)
        return ToolID::NONE;

    auto tp = two_points_from_selection(get_doc(), m_selection);
    if (!tp)
        return ToolID::NONE;

    if (all_entities_in_current_workplane(tp->get_enps()))
        return ToolID::NONE;

    return ToolID::CONSTRAIN_DISTANCE_3D;
}


ToolResponse ToolConstrainDistance::begin(const ToolArgs &args)
{
    sketch_dimension_debug_log(std::format("begin selection_count={} selection={}", m_selection.size(),
                                           describe_selection(m_selection)));
    if (m_tool_id == ToolID::CONSTRAIN_DISTANCE) {
        if (auto lp = line_and_line_from_selection(get_doc(), m_selection)) {
            sketch_dimension_debug_log("using selected line-to-line distance");
            auto &constraint = just_add_constraint<ConstraintPointLineDistance>();
            constraint.m_line = lp->line;
            constraint.m_point = lp->point;
            constraint.m_wrkpl = get_workplane_uuid();
            constraint.m_modify_to_satisfy = true;
            set_current_group_solve_pending();
            m_core.solve_current();

            prepare_interactive(constraint);
            ToolArgs move_args;
            move_args.type = ToolEventType::MOVE;
            ToolCommonConstrainDatum::update(move_args);
            ToolArgs action_args;
            action_args.type = ToolEventType::ACTION;
            action_args.action = InToolActionID::LMB;
            return ToolCommonConstrainDatum::update(action_args);
        }
        if (m_selection.size() == 1) {
            const auto &sr = *m_selection.begin();
            if (sr.type == SelectableRef::Type::ENTITY && sr.point == 0) {
                auto &entity = get_doc().get_entity(sr.item);
                if (entity.of_type(Entity::Type::ARC_2D, Entity::Type::CIRCLE_2D)) {
                    ConstraintDiameterRadius *constraint = nullptr;
                    if (entity.of_type(Entity::Type::ARC_2D))
                        constraint = &just_add_constraint<ConstraintRadius>();
                    else
                        constraint = &just_add_constraint<ConstraintDiameter>();
                    constraint->m_entity = sr.item;
                    constraint->measure(get_doc());
                    set_current_group_solve_pending();
                    m_core.solve_current();

                    prepare_interactive(*constraint);
                    ToolArgs move_args;
                    move_args.type = ToolEventType::MOVE;
                    ToolCommonConstrainDatum::update(move_args);
                    ToolArgs action_args;
                    action_args.type = ToolEventType::ACTION;
                    action_args.action = InToolActionID::LMB;
                    return ToolCommonConstrainDatum::update(action_args);
                }
            }
        }
        if (m_selection.size() == 1) {
            const auto &selection = *m_selection.begin();
            if (selection.type == SelectableRef::Type::ENTITY && selection.point == 0) {
                sketch_dimension_debug_log("selected first rectangle side; waiting for second side");
                return ToolResponse();
            }
        }
        if (auto lp = line_and_point_from_selection(get_doc(), m_selection, LineAndPoint::AllowSameEntity::NO)) {
            auto &constraint = just_add_constraint<ConstraintPointLineDistance>();
            constraint.m_line = lp->line;
            constraint.m_point = lp->point;
            constraint.m_wrkpl = get_workplane_uuid();
            constraint.m_modify_to_satisfy = true;
            set_current_group_solve_pending();
            m_core.solve_current();

            prepare_interactive(constraint);
            ToolArgs move_args;
            move_args.type = ToolEventType::MOVE;
            ToolCommonConstrainDatum::update(move_args);
            ToolArgs action_args;
            action_args.type = ToolEventType::ACTION;
            action_args.action = InToolActionID::LMB;
            return ToolCommonConstrainDatum::update(action_args);
        }
    }

    auto tp = two_points_from_selection(get_doc(), m_selection);

    if (!tp) {
        sketch_dimension_debug_log("begin failed: selection did not resolve to two points");
        return ToolResponse();
    }

    ConstraintPointDistanceBase *constraint = nullptr;
    switch (m_tool_id) {
    case ToolID::CONSTRAIN_DISTANCE_HORIZONTAL:
    case ToolID::MEASURE_DISTANCE_HORIZONTAL:
        constraint = &just_add_constraint<ConstraintPointDistanceHorizontal>();
        break;

    case ToolID::CONSTRAIN_DISTANCE_VERTICAL:
    case ToolID::MEASURE_DISTANCE_VERTICAL:
        constraint = &just_add_constraint<ConstraintPointDistanceVertical>();
        break;

    default:
        if (m_tool_id == ToolID::CONSTRAIN_DISTANCE && m_selection.size() == 1) {
            const auto &selection = *m_selection.begin();
            if (selection.type == SelectableRef::Type::ENTITY && selection.point == 0) {
                if (auto *line = dynamic_cast<EntityLine2D *>(&get_doc().get_entity(selection.item))) {
                    const auto delta = line->m_p2 - line->m_p1;
                    if (std::abs(delta.x) > std::abs(delta.y))
                        constraint = &just_add_constraint<ConstraintPointDistanceHorizontal>();
                    else
                        constraint = &just_add_constraint<ConstraintPointDistanceVertical>();
                }
            }
        }
        if (!constraint)
            constraint = &just_add_constraint<ConstraintPointDistance>();
    }

    if (any_of(m_tool_id, ToolID::MEASURE_DISTANCE, ToolID::MEASURE_DISTANCE_HORIZONTAL,
               ToolID::MEASURE_DISTANCE_VERTICAL))
        constraint->m_measurement = true;
    else
        set_current_group_solve_pending();


    constraint->m_entity1 = tp->point1;
    constraint->m_entity2 = tp->point2;
    constraint->m_wrkpl = get_workplane_uuid();
    auto dist = constraint->measure_distance(get_doc());
    if (dist < 0)
        constraint->flip();
    constraint->m_distance = std::abs(dist);
    sketch_dimension_debug_log(std::format("constraint created distance={} entity1={} entity2={}",
                                           constraint->m_distance, static_cast<std::string>(tp->point1.entity),
                                           static_cast<std::string>(tp->point2.entity)));

    auto response = prepare_interactive(*constraint);
    if (m_tool_id == ToolID::CONSTRAIN_DISTANCE) {
        // Fusion-style workflow: once a line is selected, show the preview
        // and open the value editor immediately instead of requiring a second
        // placement click.
        ToolArgs move_args;
        move_args.type = ToolEventType::MOVE;
        ToolCommonConstrainDatum::update(move_args);

        ToolArgs action_args;
        action_args.type = ToolEventType::ACTION;
        action_args.action = InToolActionID::LMB;
        response = ToolCommonConstrainDatum::update(action_args);
    }
    return response;
}
} // namespace dune3d
