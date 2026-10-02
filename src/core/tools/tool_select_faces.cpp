#include "tool_select_faces.hpp"
#include "document/document.hpp"
#include "document/group/group_face_operation.hpp"
#include <optional>
#include <algorithm>
#include "util/selection_util.hpp"
#include "editor/editor_interface.hpp"
#include "tool_common_impl.hpp"
#include "util/action_label.hpp"

namespace dune3d {

// Unlike ToolSelectEdges, no special "select mode" is needed here: faces of
// the current document's current body are already individually selectable
// in normal rendering (SelectableRef::Type::SOLID_MODEL_FACE with a stable
// per-body face index -- see Renderer's per-current-document body
// rendering, the same mechanism "attach sketch to face" already relies on),
// unlike edges, which have no normal per-edge selectable at all.

ToolBase::CanBegin ToolSelectFaces::can_begin()
{
    auto &group = get_doc().get_group(m_core.get_current_group());
    return dynamic_cast<GroupFaceOperation *>(&group);
}

ToolResponse ToolSelectFaces::begin(const ToolArgs &args)
{
    m_group = &get_doc().get_group<GroupFaceOperation>(m_core.get_current_group());
    m_selection.clear();
    for (const auto idx : m_group->m_faces) {
        m_selection.insert(SelectableRef{SelectableRef::Type::SOLID_MODEL_FACE, UUID(), idx});
    }
    m_intf.enable_hover_selection();
    {
        std::vector<ActionLabelInfo> actions;
        actions.emplace_back(InToolActionID::LMB, "select/deselect face");
        actions.emplace_back(InToolActionID::RMB, "finish");
        actions.emplace_back(InToolActionID::CANCEL, "cancel");
        actions.emplace_back(InToolActionID::CLEAR_FACES);
        m_intf.tool_bar_set_actions(actions);
    }

    m_intf.set_no_canvas_update(true);
    m_intf.canvas_update_from_tool();

    return ToolResponse();
}

ToolResponse ToolSelectFaces::update(const ToolArgs &args)
{
    if (args.type == ToolEventType::MOVE) {
        if (m_intf.get_hover_selection())
            m_intf.tool_bar_set_tool_tip("face " + std::to_string(m_intf.get_hover_selection()->point));
        else
            m_intf.tool_bar_set_tool_tip("");
    }
    else if (args.type == ToolEventType::ACTION) {
        switch (args.action) {
        case InToolActionID::LMB: {
            auto hsel = m_intf.get_hover_selection();

            if (!hsel.has_value())
                return ToolResponse();

            if (hsel->type != SelectableRef::Type::SOLID_MODEL_FACE)
                return ToolResponse();

            if (m_selection.contains(*hsel))
                m_selection.erase(*hsel);
            else
                m_selection.insert(*hsel);

        } break;

        case InToolActionID::RMB: {
            m_group->m_faces.clear();
            for (const auto &sr : m_selection) {
                if (sr.type != SelectableRef::Type::SOLID_MODEL_FACE)
                    continue;

                m_group->m_faces.insert(sr.point);
            }
            set_current_group_update_solid_model_pending();
            return ToolResponse::commit();
        }

        case InToolActionID::CLEAR_FACES:
            m_group->m_faces.clear();
            m_selection.clear();
            break;

        case InToolActionID::CANCEL:
            return ToolResponse::revert();

        default:;
        }
    }

    return ToolResponse();
}
} // namespace dune3d
