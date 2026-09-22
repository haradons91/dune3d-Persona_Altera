#include "tool_common_constrain_datum.hpp"
#include "document/constraint/iconstraint_movable.hpp"
#include "document/constraint/iconstraint_workplane.hpp"
#include "document/constraint/iconstraint_datum.hpp"
#include "document/constraint/constraint.hpp"
#include "document/entity/entity_workplane.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include "editor/editor_interface.hpp"
#include "dialogs/dialogs.hpp"
#include "dialogs/enter_datum_window.hpp"
#include "tool_common_constrain_impl.hpp"
#include "util/debug.hpp"
#include <format>

namespace dune3d {
namespace {
void sketch_dimension_debug_log(const std::string &message)
{
    debug_log(DebugCategory::UI, message);
}
} // namespace

ToolResponse ToolCommonConstrainDatum::prepare_interactive(Constraint &constraint)
{
    if (m_is_preview)
        return ToolResponse::commit();

    m_constraint_datum = &dynamic_cast<IConstraintDatum &>(constraint);
    m_constraint_movable = &dynamic_cast<IConstraintMovable &>(constraint);

    auto co_wrkpl = dynamic_cast<const IConstraintWorkplane *>(&constraint);
    if (co_wrkpl)
        if (auto wrkpl_uu = co_wrkpl->get_workplane(get_doc()))
            m_constraint_wrkpl = &get_entity<EntityWorkplane>(wrkpl_uu);

    return ToolResponse();
}

ToolResponse ToolCommonConstrainDatum::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::MOVE && !m_have_win) {
        if (m_constraint_movable->offset_is_in_workplane() && m_constraint_wrkpl) {
            auto p = m_constraint_wrkpl->project(get_cursor_pos_for_workplane(*m_constraint_wrkpl));
            m_constraint_movable->set_offset(glm::dvec3(p, 0) - m_constraint_movable->get_origin(get_doc()));
        }
        else {
            glm::dvec3 p;
            if (m_constraint_wrkpl)
                p = get_cursor_pos_for_workplane(*m_constraint_wrkpl);
            else
                p = m_intf.get_cursor_pos_for_plane(m_constraint_movable->get_origin(get_doc()),
                                                    m_intf.get_cam_normal());
            m_constraint_movable->set_offset(p - m_constraint_movable->get_origin(get_doc()));
        }
        set_first_update_group_current();
        return ToolResponse();
    }
    else if (args.type == ToolEventType::ACTION) {
        sketch_dimension_debug_log(std::format("datum action={} have_window={}", static_cast<int>(args.action),
                                               m_have_win));
        switch (args.action) {
        case InToolActionID::LMB: {
            if (m_constraint_datum->is_measurement())
                return commit();

            double def = m_constraint_datum->get_datum();
            sketch_dimension_debug_log(std::format("opening datum editor default={}", def));
            auto win = m_intf.get_dialogs().show_enter_datum_window("Enter " + m_constraint_datum->get_datum_name(),
                                                                    m_constraint_datum->get_datum_unit(), def);

            auto rng = m_constraint_datum->get_datum_range();
            win->set_range(rng.first, rng.second);
            m_have_win = true;
            sketch_dimension_debug_log("datum editor shown");
        } break;

        case InToolActionID::CANCEL:
        case InToolActionID::RMB:
            return ToolResponse::revert();

        default:;
        }
    }
    else if (args.type == ToolEventType::DATA) {
        if (auto data = dynamic_cast<const ToolDataWindow *>(args.data.get())) {
            if (data->event == ToolDataWindow::Event::UPDATE) {
                if (auto d = dynamic_cast<const ToolDataEnterDatumWindow *>(args.data.get())) {
                    sketch_dimension_debug_log(std::format("datum update value={} measurement={}", d->value,
                                                           m_constraint_datum->is_measurement()));
                    m_constraint_datum->set_datum(d->value);
                    set_current_group_solve_pending();
                    m_core.solve_current();
                }
            }
            else if (data->event == ToolDataWindow::Event::OK) {
                sketch_dimension_debug_log("datum editor OK");
                return commit();
            }
            else if (data->event == ToolDataWindow::Event::CLOSE) {
                sketch_dimension_debug_log("datum editor CLOSE");
                return ToolResponse::revert();
            }
        }
    }
    return ToolResponse();
}


} // namespace dune3d
