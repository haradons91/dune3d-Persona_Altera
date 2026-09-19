#include "editor.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include "logger/logger.hpp"
#include "dune3d_appwindow.hpp"
#include "canvas/canvas.hpp"
#include "core/tool_id.hpp"
#include <fstream>
#include <format>

namespace dune3d {
namespace {
void sketch_dimension_debug_log(const std::string &message)
{
    static std::ofstream log("/tmp/dune3d-sketch-dimension-debug.log", std::ios::app);
    log << message << '\n';
    log.flush();
}
} // namespace

bool Editor::force_end_tool()
{
    if (!m_core.tool_is_active())
        return true;

    for (auto i = 0; i < 5; i++) {
        ToolArgs args;
        args.type = ToolEventType::ACTION;
        args.action = InToolActionID::CANCEL;
        ToolResponse r = m_core.tool_update(args);
        tool_process(r);
        if (!m_core.tool_is_active())
            return true;
    }
    Logger::get().log_critical("Tool didn't end", Logger::Domain::EDITOR, "end the tool and repeat the last action");
    return false;
}


void Editor::tool_begin(ToolID id, std::unique_ptr<ToolData> data)
{
    if (m_core.tool_is_active()) {
        Logger::log_critical("can't begin tool while tool is active", Logger::Domain::EDITOR);
        return;
    }
    m_win.hide_delete_items_popup();

    ToolArgs args;
    args.data = std::move(data);

    //    if (override_selection)
    //        args.selection = sel;
    //   else
    args.selection = get_canvas().get_selection();
    // Dimension can be started with a preselected rectangle side.  That side
    // is only the first reference now, so leave the canvas selectable while
    // the tool waits for the second side.
    const bool select_dimension_after_begin = id == ToolID::CONSTRAIN_DISTANCE && args.selection.size() <= 1;
    if (id == ToolID::CONSTRAIN_DISTANCE)
        sketch_dimension_debug_log(std::format("editor tool_begin selection_count={} select_after_begin={} mode={}",
                                               args.selection.size(), select_dimension_after_begin,
                                               static_cast<int>(get_canvas().get_selection_mode())));
    m_last_selection_mode = get_canvas().get_selection_mode();
    get_canvas().set_selection_mode(SelectionMode::NONE);
    ToolResponse r = m_core.tool_begin(id, args);
    tool_process(r);
    if (select_dimension_after_begin && m_core.tool_is_active())
        get_canvas().set_selection_mode(SelectionMode::NORMAL);
    if (id == ToolID::CONSTRAIN_DISTANCE)
        sketch_dimension_debug_log(std::format("editor tool_begin complete active={} mode={}",
                                               m_core.tool_is_active(),
                                               static_cast<int>(get_canvas().get_selection_mode())));
}


void Editor::tool_update_data(std::unique_ptr<ToolData> data)
{
    if (data)
        sketch_dimension_debug_log(std::format("editor tool_update_data active={} data_type={}",
                                               m_core.tool_is_active(), typeid(*data).name()));
    if (m_core.tool_is_active()) {
        ToolArgs args;
        args.type = ToolEventType::DATA;
        args.data = std::move(data);
        ToolResponse r = m_core.tool_update(args);
        tool_process(r);
    }
}


void Editor::tool_process(ToolResponse &resp)
{
    tool_process_one();
    while (auto args = m_core.get_pending_tool_args()) {
        m_core.tool_update(*args);

        tool_process_one();
    }
}

void Editor::canvas_update_from_tool()
{
    canvas_update();
    get_canvas().set_selection(m_core.get_tool_selection(), false);
}

void Editor::tool_process_one()
{
    if (!m_core.tool_is_active()) {
        m_no_canvas_update = false;
        m_constraint_tip_icons.clear();
        m_solid_model_edge_select_mode = false;
    }
    if (!m_no_canvas_update)
        canvas_update();
    get_canvas().set_selection(m_core.get_tool_selection(), false);
    if (!m_core.tool_is_active()) {
        m_dialogs.close_nonmodal();
        // imp_interface->dialogs.close_nonmodal();
        // reset_tool_hint_label();
        // canvas->set_cursor_external(false);
        // canvas->snap_filter.clear();
        update_workplane_label();
        update_selection_editor();
        update_action_bar_buttons_sensitivity(); // due to workplane change
    }

    if (!m_core.tool_is_active())
        get_canvas().set_selection_mode(m_last_selection_mode);

    /*  if (m_core.tool_is_active()) {
          canvas->set_selection(m_core.get_tool_selection());
      }
      else {
          canvas->set_selection(m_core.get_tool_selection(),
                                canvas->get_selection_mode() == CanvasGL::SelectionMode::NORMAL);
      }*/
}

void Editor::handle_tool_change()
{
    const auto tool_id = m_core.get_tool_id();
    // panels->set_sensitive(id == ToolID::NONE);
    // canvas->set_selection_allowed(id == ToolID::NONE);
    // main_window->tool_bar_set_use_actions(core->get_tool_actions().size());

    // For most tools (e.g. draw contour) it is quite confusing if everything
    // except the solid models is hidden. Therefore disable that mode when
    // starting a tool.
    set_show_only_solid_models(false);
    if (tool_id != ToolID::NONE) {
        m_win.tool_bar_set_tool_name(action_catalog.at(tool_id).name.full);
        tool_bar_set_tool_tip("");
    }
    m_win.tool_bar_set_visible(tool_id != ToolID::NONE);
    tool_bar_clear_actions();
    update_action_bar_visibility();
}

} // namespace dune3d
