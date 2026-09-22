#include "editor.hpp"
#include "dune3d_appwindow.hpp"
#include "widgets/sketch_plane_selector.hpp"
#include "core/tool_id.hpp"
#include "widgets/constraints_box.hpp"
#include "action/action_id.hpp"
#include "in_tool_action/in_tool_action.hpp"
#include "in_tool_action/in_tool_action_catalog.hpp"
#include "canvas/canvas.hpp"
#include "document/entity/entity.hpp"
#include "document/entity/entity_document.hpp"
#include "tool_popover.hpp"
#include "dune3d_application.hpp"
#include "util/selection_util.hpp"
#include "group_editor/group_editor.hpp"
#include "render/renderer.hpp"
#include "document/entity/entity_workplane.hpp"
#include "document/entity/entity_step.hpp"
#include "document/group/group_reference.hpp"
#include "document/group/group_extrude.hpp"
#include "document/group/group_sketch.hpp"
#include "document/solid_model/solid_model.hpp"
#include "logger/logger.hpp"
#include "document/constraint/constraint.hpp"
#include "util/fs_util.hpp"
#include "util/util.hpp"
#include "selection_editor.hpp"
#include "preferences/color_presets.hpp"
#include "workspace_browser.hpp"
#include "document/constraint/iconstraint_workplane.hpp"
#include "widgets/clipping_plane_window.hpp"
#include "widgets/selection_filter_window.hpp"
#include "dialogs/rectangle_dimensions_window.hpp"
#include "system/system.hpp"
#include "logger/log_util.hpp"
#include "util/debug.hpp"
#include "nlohmann/json.hpp"
#include "buffer.hpp"
#include <iostream>
#include <format>

namespace dune3d {
namespace {
void sketch_dimension_debug_log(const std::string &message)
{
    debug_log(DebugCategory::UI, message);
}
} // namespace

Editor::CanvasUpdater::CanvasUpdater(Editor &editor) : m_editor(editor)
{
    m_editor.m_canvas_update_pending++;
}

Editor::CanvasUpdater::~CanvasUpdater()
{
    if (m_editor.m_canvas_update_pending == 0)
        return; // should not happen
    m_editor.m_canvas_update_pending--;
    if (m_editor.m_canvas_update_pending == 0)
        m_editor.canvas_update_keep_selection();
}

Editor::Editor(Dune3DAppWindow &win, Preferences &prefs)
    : m_preferences(prefs), m_dialogs(win, *this), m_win(win), m_core(*this), m_selection_menu_creator(m_core)
{
    m_drag_tool = ToolID::NONE;
}

Editor::~Editor() = default;

void Editor::ensure_new_document()
{
    if (!m_core.has_documents())
        trigger_action(ActionID::NEW_DOCUMENT);
}

void Editor::update_document_tabs()
{
    auto &tabs = m_win.get_window_document_tabs();
    while (auto child = tabs.get_first_child())
        tabs.remove(*child);

    const auto documents = m_core.get_documents();
    std::set<UUID> current_documents;
    for (auto doc : documents)
        current_documents.insert(doc->get_uuid());

    for (auto doc : documents) {
        if (std::find(m_document_tab_order.begin(), m_document_tab_order.end(), doc->get_uuid())
            == m_document_tab_order.end())
            m_document_tab_order.push_back(doc->get_uuid());
    }
    std::erase_if(m_document_tab_order, [&current_documents](const UUID &uuid) {
        return !current_documents.contains(uuid);
    });

    if (documents.empty())
        return;

    const auto current_uuid = m_core.get_current_idocument_info().get_uuid();
    for (const auto &uuid : m_document_tab_order) {
        auto doc = std::find_if(documents.begin(), documents.end(), [&uuid](auto candidate) {
            return candidate->get_uuid() == uuid;
        });
        if (doc == documents.end())
            continue;
        auto tab_pair = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
        tab_pair->add_css_class("document-tab-pair");
        if (uuid == current_uuid)
            tab_pair->add_css_class("active");
        auto tab = Gtk::make_managed<Gtk::ToggleButton>();
        auto close_button = Gtk::make_managed<Gtk::Button>();
        close_button->set_icon_name("window-close-symbolic");
        close_button->set_tooltip_text("Close document");
        close_button->set_has_frame(false);
        close_button->set_focusable(false);
        close_button->add_css_class("document-tab-close");
        tab->set_label((*doc)->get_name());
        tab->set_active(uuid == current_uuid);
        tab->add_css_class("document-tab");
        close_button->signal_clicked().connect([this, uuid] {
            // Closing can rebuild the tab row, so wait until this button's
            // signal has finished dispatching before removing its widget.
            Glib::signal_idle().connect_once([this, uuid] {
                const auto documents = m_core.get_documents();
                if (std::ranges::any_of(documents, [&uuid](auto doc) { return doc->get_uuid() == uuid; }))
                    close_document(uuid, nullptr, nullptr);
            });
        });
        tab->signal_clicked().connect([this, uuid, tab] {
            UUID target_workspace_view;
            for (const auto &[workspace_uuid, workspace_view] : m_workspace_views) {
                if (workspace_view.m_current_document == uuid) {
                    target_workspace_view = workspace_uuid;
                    break;
                }
            }
            if (target_workspace_view) {
                set_current_workspace_view(target_workspace_view);
            }
            show_workspace_browser(uuid);
            auto &tabs = m_win.get_window_document_tabs();
            for (auto pair = tabs.get_first_child(); pair; pair = pair->get_next_sibling()) {
                if (auto other_tab = dynamic_cast<Gtk::ToggleButton *>(pair))
                    other_tab->set_active(other_tab == tab);
                else if (auto tab_pair = dynamic_cast<Gtk::Box *>(pair)) {
                    if (auto other_tab = dynamic_cast<Gtk::ToggleButton *>(tab_pair->get_first_child()))
                        other_tab->set_active(other_tab == tab);
                    tab_pair->set_css_classes({"document-tab-pair"});
                    if (tab_pair->get_first_child() == tab)
                        tab_pair->add_css_class("active");
                }
            }
        });
        tab_pair->append(*tab);
        tab_pair->append(*close_button);
        tabs.append(*tab_pair);
    }
}

void Editor::update_timeline()
{
    DUNE3D_TRACE(DebugCategory::TIMELINE);
    auto &timeline = m_win.get_timeline_items_box();
    while (auto child = timeline.get_first_child())
        timeline.remove(*child);

    if (!m_core.has_documents())
        return;

    const auto current_uuid = m_core.get_current_idocument_info().get_uuid();
    const auto &doc = m_core.get_current_document();
    const auto current_group = m_core.get_current_group();
    for (auto group : doc.get_groups_sorted()) {
        if (group->get_type() == Group::Type::REFERENCE)
            continue;
        bool imported_step_group = group->get_type() == Group::Type::STEP;
        for (const auto &[entity_uuid, entity] : doc.m_entities) {
            if (entity->m_group == group->m_uuid && dynamic_cast<const EntitySTEP *>(entity.get())) {
                imported_step_group = true;
                break;
            }
        }
        auto feature = Gtk::make_managed<Gtk::ToggleButton>();
        feature->set_label(imported_step_group ? (group->m_name.empty() ? "STEP" : group->m_name)
                                               : (group->m_name.empty() ? group->get_type_name() : group->m_name));
        feature->set_active(group->m_uuid == current_group);
        feature->set_tooltip_text(imported_step_group ? "Imported STEP body" : group->get_type_name());
        feature->add_css_class("dune3d-timeline-feature");
        feature->signal_clicked().connect([this, current_uuid, uu = group->m_uuid] {
            on_workspace_browser_group_selected(current_uuid, uu);
            if (m_workspace_browser)
                m_workspace_browser->select_group(uu);
        });
        timeline.append(*feature);
    }
}

void Editor::init()
{
    m_win.init_rectangle_dimensions(*this);
    m_win.get_sketch_plane_selector().signal_plane_selected().connect([this](SketchPlaneSelector::Plane plane) {
        const auto &reference = m_core.get_current_document().get_reference_group();
        UUID plane_uuid;
        switch (plane) {
        case SketchPlaneSelector::Plane::XY:
            plane_uuid = reference.get_workplane_xy_uuid();
            break;
        case SketchPlaneSelector::Plane::YZ:
            plane_uuid = reference.get_workplane_yz_uuid();
            break;
        case SketchPlaneSelector::Plane::ZX:
            plane_uuid = reference.get_workplane_zx_uuid();
            break;
        }
        finish_sketch_plane_selection(plane_uuid);
    });
    get_canvas().signal_hover_selection_changed().connect([this] {
        if (m_selecting_sketch_plane) {
            Glib::signal_idle().connect_once([this] {
                if (m_selecting_sketch_plane)
                    canvas_update();
            });
        }
    });
    init_workspace_browser();
    init_properties_notebook();
    init_header_bar();
    init_actions();
    init_tool_popover();
    init_canvas();
    update_document_tabs();

    m_core.signal_needs_save().connect([this] {
        update_action_sensitivity();
        if (m_workspace_browser)
            m_workspace_browser->update_needs_save();
        update_workspace_view_names();
    });
    get_canvas().signal_selection_changed().connect([this] {
        update_action_sensitivity();
        const auto &selection = get_canvas().get_selection();
        if (std::ranges::any_of(selection, [](const auto &item) {
                return item.type == SelectableRef::Type::SKETCH_PROFILE;
            }))
            canvas_update_keep_selection();
    });

    m_win.signal_close_request().connect(
            [this] {
                if (!m_core.get_needs_save_any())
                    return false;

                auto cb = [this] {
                    // here, the close dialog is still there and closing the main window causes a near-segfault
                    // so break out of the current event
                    Glib::signal_idle().connect_once([this] { m_win.close(); });
                };
                close_document(m_core.get_current_idocument_info().get_uuid(), cb, cb);

                return true; // keep window open
            },
            true);


    update_workplane_label();


    m_preferences.signal_changed().connect(sigc::mem_fun(*this, &Editor::apply_preferences));

    m_core.signal_tool_changed().connect(sigc::mem_fun(*this, &Editor::handle_tool_change));


    m_core.signal_documents_changed().connect([this] {
        for (auto doc : m_core.get_documents()) {
            for (auto &[uu, wsv] : m_workspace_views) {
                wsv.m_documents[doc->get_uuid()];
            }
        }
        m_win.get_workspace_notebook().set_visible(m_core.has_documents());
        CanvasUpdater canvas_updater{*this};
        for (auto doc : m_core.get_documents())
            ensure_workspace_browser(doc->get_uuid());
        if (m_core.has_documents())
            show_workspace_browser(m_core.get_current_idocument_info().get_uuid());
        update_group_editor();
        update_workplane_label();
        update_action_sensitivity();
        // Keep the welcome overlay hidden after startup; closing the last
        // document should not reopen the Open Recent panel.
        m_win.set_welcome_box_visible(false);
        update_version_info();
        update_action_bar_buttons_sensitivity();
        update_action_bar_visibility();
        update_selection_editor();
        update_title();
        update_document_tabs();
        update_timeline();
    });

    attach_action_button(m_win.get_welcome_open_button(), ActionID::OPEN_DOCUMENT);
    attach_action_button(m_win.get_welcome_new_button(), ActionID::NEW_DOCUMENT);
    attach_action_button(m_win.get_window_new_document_tab_button(), ActionID::NEW_DOCUMENT);

    // Drawing tools are provided by the ribbon. Keep the perspective
    // viewport free of the duplicate floating tool buttons.

    init_view_options();

    m_clipping_plane_window = std::make_unique<ClippingPlaneWindow>();
    m_clipping_plane_window->set_transient_for(m_win);
    connect_action(ActionID::CLIPPING_PLANE_WINDOW, [this](const auto &a) { m_clipping_plane_window->present(); });
    connect_action(ActionID::TOGGLE_CLIPPING_PLANES,
                   [this](const auto &a) { m_clipping_plane_window->toggle_global(); });
    m_clipping_plane_window->signal_changed().connect([this] {
        get_canvas().set_clipping_planes(m_clipping_plane_window->get_planes());
        update_view_hints();
    });
    m_clipping_plane_window->set_hide_on_close(true);

    m_selection_filter_window = std::make_unique<SelectionFilterWindow>(m_core);
    m_selection_filter_window->set_transient_for(m_win);
    m_selection_filter_window->set_hide_on_close(true);
    connect_action(ActionID::SELECTION_FILTER, [this](const auto &a) { m_selection_filter_window->present(); });
    get_canvas().set_selection_filter(*m_selection_filter_window);
    m_selection_filter_window->signal_changed().connect(sigc::mem_fun(*this, &Editor::update_view_hints));

    connect_action(ActionID::SELECT_UNDERCONSTRAINED, [this](const auto &a) {
        auto &doc = m_core.get_current_document();
        System sys{doc, m_core.get_current_group()};
        std::set<EntityAndPoint> free_points;
        sys.solve(&free_points);
        std::set<SelectableRef> sel;
        for (const auto &enp : free_points) {
            sel.emplace(SelectableRef::Type::ENTITY, enp.entity, enp.point);
        }
        get_canvas().set_selection(sel, true);
        get_canvas().set_selection_mode(SelectionMode::NORMAL);
    });

    m_win.signal_undo().connect([this] { trigger_action(ActionID::UNDO); });

    update_action_sensitivity();
    reset_key_hint_label();

    m_win.get_workspace_add_button().signal_clicked().connect([this] {
        auto new_wv_uu = create_workspace_view_from_current();
        set_current_workspace_view(new_wv_uu);
    });

    m_win.get_canvas().signal_view_changed().connect([this] {
        if (!m_current_workspace_view)
            return;
        if (m_workspace_view_loading)
            return;
        auto &wv = m_workspace_views.at(m_current_workspace_view);
        auto &ca = m_win.get_canvas();
        wv.m_cam_distance = ca.get_cam_distance();
        wv.m_cam_quat = ca.get_cam_quat();
        wv.m_center = ca.get_center();
        wv.m_projection = ca.get_projection();
        if (m_sketch_editing)
            canvas_update_keep_selection();
    });

    m_win.get_workspace_notebook().signal_switch_page().connect([this](Gtk::Widget *page, guint index) {
        auto &pg = dynamic_cast<WorkspaceViewPage &>(*page);
        set_current_workspace_view(pg.m_uuid);
    });

    apply_preferences();
}

void Editor::add_tool_action(ActionToolID id, const std::string &action)
{
    m_win.add_action(action, [this, id] { trigger_action(id); });
}

void Editor::init_view_options()
{
    auto view_options_popover = Gtk::make_managed<Gtk::PopoverMenu>();
    m_win.get_view_options_button().set_popover(*view_options_popover);
    {
        Gdk::Rectangle rect;
        rect.set_width(32);
        m_win.get_view_options_button().get_popover()->set_pointing_to(rect);
    }

    m_view_options_menu = Gio::Menu::create();
    m_perspective_action = m_win.add_action_bool("perspective", false);
    m_perspective_action->signal_change_state().connect([this](const Glib::VariantBase &v) {
        auto b = Glib::VariantBase::cast_dynamic<Glib::Variant<bool>>(v).get();
        set_perspective_projection(b);
    });
    m_previous_construction_entities_action = m_win.add_action_bool("previous_construction", false);
    m_previous_construction_entities_action->signal_change_state().connect([this](const Glib::VariantBase &v) {
        auto b = Glib::VariantBase::cast_dynamic<Glib::Variant<bool>>(v).get();
        set_show_previous_construction_entities(b);
    });
    m_show_only_solid_models_action = m_win.add_action_bool("show_only_solid_models", false);
    m_show_only_solid_models_action->signal_change_state().connect([this](const Glib::VariantBase &v) {
        auto b = Glib::VariantBase::cast_dynamic<Glib::Variant<bool>>(v).get();
        set_show_only_solid_models(b);
    });
    m_hide_irrelevant_workplanes_action = m_win.add_action_bool("irrelevant_workplanes", false);
    m_hide_irrelevant_workplanes_action->signal_change_state().connect([this](const Glib::VariantBase &v) {
        auto b = Glib::VariantBase::cast_dynamic<Glib::Variant<bool>>(v).get();
        set_hide_irrelevant_workplanes(b);
    });

    add_tool_action(ActionID::CLIPPING_PLANE_WINDOW, "clipping_planes");
    add_tool_action(ActionID::SELECTION_FILTER, "selection_filter");

    m_view_options_menu->append("Selection filter", "win.selection_filter");
    m_view_options_menu->append("Clipping planes", "win.clipping_planes");
    m_view_options_menu->append("Previous construction entities", "win.previous_construction");
    m_view_options_menu->append("Only solid models", "win.show_only_solid_models");
    m_view_options_menu->append("Hide irrelevant workplanes", "win.irrelevant_workplanes");
    m_view_options_menu->append("Perspective projection", "win.perspective");
    {
        auto it = Gio::MenuItem::create("scale", "scale");
        it->set_attribute_value("custom", Glib::Variant<Glib::ustring>::create("scale"));

        m_view_options_menu->append_item(it);
    }

    view_options_popover->set_menu_model(m_view_options_menu);

    {
        auto adj = Gtk::Adjustment::create(-1, -1, 5, .01, .1);
        m_curvature_comb_scale = Gtk::make_managed<Gtk::Scale>(adj);
        m_curvature_comb_scale->add_mark(adj->get_lower(), Gtk::PositionType::BOTTOM, "Off");

        adj->signal_value_changed().connect([this, adj] {
            if (!m_core.has_documents())
                return;
            float scale = 0;
            auto val = adj->get_value();
            if (val > adj->get_lower())
                scale = powf(10, val);
            auto &wv = m_workspace_views.at(m_current_workspace_view);
            wv.m_curvature_comb_scale = scale;
            CanvasUpdater canvas_updater{*this};
            update_view_hints();
        });

        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
        box->set_margin_start(32);
        box->set_margin_top(6);
        auto label = Gtk::make_managed<Gtk::Label>("Curvature comb scale");
        label->set_xalign(0);
        box->append(*label);
        box->append(*m_curvature_comb_scale);

        view_options_popover->add_child(*box, "scale");
    }
}

Gtk::Button &Editor::create_action_bar_button(ActionToolID action)
{
    static const std::map<ActionToolID, std::string> action_icons = {
            {ToolID::DRAW_CONTOUR, "action-draw-contour-symbolic"},
            {ToolID::DRAW_CIRCLE_2D, "action-draw-line-circle-symbolic"},
            {ToolID::DRAW_RECTANGLE, "action-draw-line-rectangle-symbolic"},
            {ToolID::DRAW_REGULAR_POLYGON, "action-draw-line-regular-polygon-symbolic"},
            {ToolID::DRAW_WORKPLANE, "action-draw-workplane-symbolic"},
            {ToolID::DRAW_TEXT, "action-draw-text-symbolic"},
    };
    auto bu = Gtk::make_managed<Gtk::Button>();
    auto img = Gtk::make_managed<Gtk::Image>();
    if (action_icons.count(action))
        img->set_from_icon_name(action_icons.at(action));
    else
        img->set_from_icon_name("face-worried-symbolic");
    img->set_icon_size(Gtk::IconSize::LARGE);
    bu->set_child(*img);
    bu->add_css_class("osd");
    bu->add_css_class("action-button");
    bu->signal_clicked().connect([this, action] {
        if (force_end_tool())
            trigger_action(action);
    });
    m_win.add_action_button(*bu);
    m_action_bar_buttons.emplace(action, bu);
    return *bu;
}

void Editor::update_action_bar_buttons_sensitivity()
{
    auto has_docs = m_core.has_documents();
    for (auto &[act, bu] : m_action_bar_buttons) {
        auto sensitive = false;
        if (has_docs) {
            if (std::holds_alternative<ActionID>(act)) {
                auto a = std::get<ActionID>(act);
                if (m_action_sensitivity.contains(a))
                    sensitive = m_action_sensitivity.at(a);
            }
            else {
                sensitive = m_core.tool_can_begin(std::get<ToolID>(act), {}).get_can_begin();
            }
        }
        bu->set_sensitive(sensitive);
    }
}

void Editor::update_action_bar_visibility()
{
    bool visible = false;
    if (m_preferences.action_bar.enable && m_core.has_documents()) {
        auto tool_is_active = m_core.tool_is_active();
        if (m_preferences.action_bar.show_in_tool)
            visible = true;
        else
            visible = !tool_is_active;
    }
    m_win.set_action_bar_visible(visible);
}

void Editor::update_sketch_mode_ui()
{
    const bool sketch_active = m_sketch_editing && m_core.has_documents()
                               && m_core.get_current_document().get_group(m_core.get_current_group()).get_type()
                                          == Group::Type::SKETCH;
    const bool extrusion_active = m_extrude_editing && m_core.has_documents()
                                  && m_core.get_current_document().get_group(m_core.get_current_group()).get_type()
                                             == Group::Type::EXTRUDE;
    get_canvas().set_selection_peeling_enabled(!sketch_active);
    m_win.get_ribbon_create_group().set_visible(!sketch_active && !extrusion_active);
    m_win.get_ribbon_modify_group().set_visible(!sketch_active);
    m_win.get_ribbon_sketch_group().set_visible(sketch_active);
    m_win.get_ribbon_sketch_modify_group().set_visible(sketch_active);
    m_win.get_ribbon_body_inspect_group().set_visible(!sketch_active);
    m_win.get_ribbon_sketch_inspect_group().set_visible(sketch_active);
    m_win.get_finish_sketch_group().set_visible(sketch_active || extrusion_active);
    m_win.get_finish_sketch_button().set_visible(sketch_active || extrusion_active);
    m_win.get_finish_sketch_label().set_text(sketch_active ? "SKETCH" : "EXTRUDE");
    if (sketch_active)
        m_win.get_fusion_ribbon_bar().reorder_child_after(m_win.get_ribbon_sketch_group(),
                                                          m_win.get_ribbon_workspace_separator());
    else
        m_win.get_fusion_ribbon_bar().reorder_child_after(m_win.get_ribbon_sketch_group(),
                                                          m_win.get_ribbon_modify_group());
}

static std::string action_tool_id_to_string(ActionToolID id)
{
    if (auto tool = std::get_if<ToolID>(&id))
        return "T_" + tool_lut.lookup_reverse(*tool);
    else if (auto act = std::get_if<ActionID>(&id))
        return "A_" + action_lut.lookup_reverse(*act);
    throw std::runtime_error("invalid action");
}

void Editor::init_canvas()
{
    {
        auto controller = Gtk::EventControllerKey::create();
        controller->signal_key_pressed().connect(
                [this, controller](guint keyval, guint keycode, Gdk::ModifierType state) -> bool {
                    return handle_action_key(controller, keyval, state);
                },
                true);

        get_canvas().add_controller(controller);
    }
    {
        // The ribbon button keeps keyboard focus after a tool is activated.
        // Listen at the window level as well so Escape cancels immediately,
        // without requiring the user to focus the canvas first.
        auto controller = Gtk::EventControllerKey::create();
        controller->signal_key_pressed().connect(
                [this, controller](guint keyval, guint keycode, Gdk::ModifierType state) -> bool {
                    return handle_action_key(controller, keyval, state);
                },
                true);
        m_win.add_controller(controller);
    }
    {
        auto controller = Gtk::GestureClick::create();
        controller->set_button(0);
        controller->signal_pressed().connect([this, controller](int n_press, double x, double y) {
            auto button = controller->get_current_button();
            if (n_press == 2 && button == 2) {
                trigger_action(ActionID::VIEW_RESET_TILT);
                return;
            }
            if (button == 3) {
                m_rmb_last_x = x;
                m_rmb_last_y = y;
            }
#ifdef G_OS_WIN32
            static const int button_next = 4;
            static const int button_prev = 5;
#else
            static const int button_next = 8;
            static const int button_prev = 9;
#endif
            if (button == 1 /*|| button == 3*/)
                handle_click(button, n_press);
            else if (button == button_next)
                trigger_action(ActionID::NEXT_GROUP);
            else if (button == button_prev)
                trigger_action(ActionID::PREVIOUS_GROUP);
        });

        controller->signal_released().connect([this, controller](int n_press, double x, double y) {
            m_drag_tool = ToolID::NONE;
            if (m_extrude_dragging && m_extrude_drag_changed)
                m_core.rebuild("extrusion handle moved");
            if (m_extrude_dragging)
                get_canvas().set_selection_mode(SelectionMode::NORMAL);
            m_extrude_dragging = false;
            m_extrude_drag_changed = false;
            const auto button = controller->get_current_button();
            if (button == 1 && n_press == 1) {
                if (m_core.tool_is_active()) {
                    ToolArgs args;
                    args.type = ToolEventType::ACTION;
                    args.action = InToolActionID::LMB;
                    // A dimension tool may be activated before the geometry
                    // is selected. Pass the current canvas selection so the
                    // first click can choose the line to dimension.
                    args.selection = get_canvas().get_selection();
                    if (m_core.get_tool_id() == ToolID::CONSTRAIN_DISTANCE) {
                        // Once Dimension is active, its tool selection is the
                        // authoritative first item.  The canvas selection can
                        // be cleared while the tool waits for the second
                        // click, especially when Ctrl-clicking another edge.
                        // Combine the existing tool item with the current
                        // hover so two edges from nested rectangles reach the
                        // constraint tool together.
                        args.selection = m_core.get_tool_selection();
                    }
                    if (auto hover_selection = get_canvas().get_hover_selection())
                        args.selection.insert(*hover_selection);
                    if (m_core.get_tool_id() == ToolID::CONSTRAIN_DISTANCE) {
                        const auto state = controller->get_current_event_state();
                        args.m_keep_selection = (state & (Gdk::ModifierType::CONTROL_MASK
                                                          | Gdk::ModifierType::SHIFT_MASK))
                                                != Gdk::ModifierType{};
                    }
                    if (m_core.get_tool_id() == ToolID::CONSTRAIN_DISTANCE)
                        sketch_dimension_debug_log(std::format(
                                "editor canvas_click selection_count={} hover={} mode={}", args.selection.size(),
                                get_canvas().get_hover_selection().has_value(),
                                static_cast<int>(get_canvas().get_selection_mode())));
                    ToolResponse r = m_core.tool_update(args);
                    tool_process(r);
                }
            }
            else if (button == 3 && n_press == 1) {
                const auto dist = glm::length(glm::vec2(x, y) - glm::vec2(m_rmb_last_x, m_rmb_last_y));
                if (dist > 16)
                    return;
                if (m_core.tool_is_active()) {
                    ToolArgs args;
                    args.type = ToolEventType::ACTION;
                    args.action = InToolActionID::RMB;
                    ToolResponse r = m_core.tool_update(args);
                    tool_process(r);
                }
                else
                    open_context_menu();
            }
        });

        get_canvas().add_controller(controller);
    }
    {
        auto controller = Gtk::EventControllerMotion::create();
        controller->signal_motion().connect([this](double x, double y) {
            if (m_last_x != x || m_last_y != y) {
                m_last_x = x;
                m_last_y = y;
            }
        });
        get_canvas().add_controller(controller);
    }
    get_canvas().signal_cursor_moved().connect(sigc::mem_fun(*this, &Editor::handle_cursor_move));
    get_canvas().signal_view_changed().connect(sigc::mem_fun(*this, &Editor::handle_view_changed));
    get_canvas().signal_select_from_menu().connect([this](const auto &sel) {
        if (m_core.tool_is_active()) {
            ToolArgs args;
            args.type = ToolEventType::ACTION;
            args.action = InToolActionID::LMB;
            ToolResponse r = m_core.tool_update(args);
            tool_process(r);
        }
    });

    m_context_menu = Gtk::make_managed<Gtk::PopoverMenu>();
#if GTK_CHECK_VERSION(4, 14, 0)
    gtk_popover_menu_set_flags(m_context_menu->gobj(), GTK_POPOVER_MENU_NESTED);
#endif
    m_context_menu->set_parent(get_canvas());
    m_context_menu->signal_closed().connect([this] {
        if (m_core.reset_preview()) {
            canvas_update_keep_selection();
            m_context_menu->set_opacity(1);
        }
    });

    auto actions = Gio::SimpleActionGroup::create();
    for (const auto &[id, act] : action_catalog) {
        std::string name;
        auto action_id = id;
        actions->add_action(action_tool_id_to_string(id), [this, action_id] {
            get_canvas().set_selection(m_context_menu_selection, false);
            trigger_action(action_id);
        });
    }

    actions->add_action_with_parameter(
            "remove_constraint", Glib::Variant<std::string>::variant_type(), [this](Glib::VariantBase const &value) {
                UUID uu = Glib::VariantBase::cast_dynamic<Glib::Variant<std::string>>(value).get();
                auto &doc = m_core.get_current_document();
                doc.m_constraints.erase(uu);
                doc.set_group_solve_pending(m_core.get_current_group());
                m_core.set_needs_save();
                m_core.rebuild("remove constraint");
                canvas_update_keep_selection();
            });
    m_context_menu->insert_action_group("menu", actions);


    get_canvas().signal_hover_selection_changed().connect([this] {
        auto hsel = get_canvas().get_hover_selection();
        if (!hsel) {
            get_canvas().set_highlight({});
            return;
        }
        if (hsel->type != SelectableRef::Type::CONSTRAINT) {
            get_canvas().set_highlight({});
            return;
        }
        auto &constraint = m_core.get_current_document().get_constraint(hsel->item);
        std::set<SelectableRef> sel;
        std::map<UUID, std::set<unsigned int>> enps;
        UUID constraint_wrkpl;
        if (auto co_wrkpl = dynamic_cast<const IConstraintWorkplane *>(&constraint))
            constraint_wrkpl = co_wrkpl->get_workplane(m_core.get_current_document());
        for (const auto &enp : constraint.get_referenced_entities_and_points()) {
            // ignore constraint workplanes
            if (enp.entity == constraint_wrkpl)
                continue;
            sel.emplace(SelectableRef::Type::ENTITY, enp.entity, enp.point);
            if (enp.point != 0)
                sel.emplace(SelectableRef::Type::ENTITY, enp.entity, 0);
            enps[enp.entity].insert(enp.point);
        }
        for (const auto &[uu, pts] : enps) {
            auto &entity = m_core.get_current_document().get_entity(uu);
            if (entity.of_type(Entity::Type::LINE_2D, Entity::Type::LINE_3D)) {
                if (pts.contains(1) && pts.contains(2)) {
                    sel.emplace(SelectableRef::Type::ENTITY, uu, 0);
                }
            }
        }
        get_canvas().set_highlight(sel);
    });

    get_canvas().signal_selection_mode_changed().connect(sigc::mem_fun(*this, &Editor::update_selection_mode_label));
    update_selection_mode_label();

    get_canvas().signal_query_tooltip().connect(
            [this](int x, int y, bool keyboard_tooltip, const Glib::RefPtr<Gtk::Tooltip> &tooltip) {
                if (keyboard_tooltip)
                    return false;
                auto sr = get_canvas().get_hover_selection();
                if (!sr.has_value())
                    return false;

                auto tip = get_selectable_ref_description(m_core, m_core.get_current_idocument_info().get_uuid(), *sr);
                tooltip->set_text(tip);

                return true;
            },
            true);
    get_canvas().set_has_tooltip(true);

    get_canvas().set_selection_menu_creator(m_selection_menu_creator);

    /*
     * we want the canvas click event controllers to run before the one in the editor,
     * so we need to attach them afterwards since event controllers attached last run first
     */
    get_canvas().setup_controllers();
}

void Editor::update_selection_mode_label()
{
    const auto mode = get_canvas().get_selection_mode();
    std::string label;
    switch (mode) {
    case SelectionMode::HOVER:
        m_win.set_selection_mode_label_text("Hover select");
        break;
    case SelectionMode::NORMAL:
        m_win.set_selection_mode_label_text("Click select");
        break;
    default:;
    }
}

void Editor::install_hover(Gtk::Button &button, ToolID id)
{
    auto ctrl = Gtk::EventControllerMotion::create();

    ctrl->signal_leave().connect([this] {
        m_context_menu_hover_timeout.disconnect();
        m_context_menu_hover_timeout = Glib::signal_timeout().connect(
                [this] {
                    if (m_core.reset_preview()) {
                        canvas_update_keep_selection();
                        m_context_menu->set_opacity(1);
                    }
                    return false;
                },
                200);
    });
    ctrl->signal_motion().connect([this, id](double, double) {
        m_context_menu_hover_timeout.disconnect();
        if (m_core.get_current_preview_tool() == ToolID::NONE) {
            m_context_menu_hover_timeout = Glib::signal_timeout().connect(
                    [this, id] {
                        if (m_core.apply_preview(id, m_context_menu_selection)) {
                            m_context_menu->set_opacity(.5);
                            canvas_update_keep_selection();
                        }
                        return false;
                    },
                    200);
        }
        else {
            if (m_core.apply_preview(id, m_context_menu_selection))
                canvas_update_keep_selection();
        }
    });
    button.add_controller(ctrl);
}

void Editor::open_context_menu(ContextMenuMode mode)
{
    Gdk::Rectangle rect;
    rect.set_x(m_last_x);
    rect.set_y(m_last_y);
    m_context_menu->set_pointing_to(rect);
    get_canvas().end_pan();
    auto menu = Gio::Menu::create();
    auto sel = get_canvas().get_selection();
    if (mode != ContextMenuMode::CONSTRAIN) {
        auto hover_sel = get_canvas().get_hover_selection();
        if (!hover_sel)
            return;
        if (!sel.contains(*hover_sel))
            sel = {*hover_sel};
    }
    m_context_menu_selection = sel;
    update_action_sensitivity(sel);
    struct ActionInfo {
        ActionToolID id;
        ToolID equivalent_tool = ToolID::NONE;
        bool can_preview = false;
        ToolID force_unset_workplane_tool = ToolID::NONE;
        bool constraint_is_in_workplane = false;
    };
    std::vector<ActionInfo> ids;

    std::list<Glib::RefPtr<Gio::MenuItem>> meas_items;
    for (const auto &[action_group, action_group_name] : action_group_catalog) {
        if (mode == ContextMenuMode::CONSTRAIN && action_group != ActionGroup::CONSTRAIN)
            continue;
        for (const auto &[id, it_cat] : action_catalog) {
            if (it_cat.group == action_group && !(it_cat.flags & ActionCatalogItem::FLAGS_NO_MENU)) {
                if (auto tool = std::get_if<ToolID>(&id)) {
                    auto r = m_core.tool_can_begin(*tool, sel);
                    if (r.can_begin.can_begin == ToolBase::CanBegin::YES && r.is_specific) {
                        ids.emplace_back(id, r.can_begin.equivalent_tool, r.can_preview, r.force_unset_workplane_tool,
                                         r.constraint_is_in_workplane);
                        auto item = Gio::MenuItem::create(it_cat.name.menu, "menu." + action_tool_id_to_string(id));
                        if (it_cat.group == ActionGroup::MEASURE) {
                            meas_items.push_back(item);
                        }
                        else {
                            item->set_attribute_value(
                                    "custom", Glib::Variant<Glib::ustring>::create(action_tool_id_to_string(id)));
                            menu->append_item(item);
                        }
                    }
                }
                else if (auto act = std::get_if<ActionID>(&id)) {
                    if (get_action_sensitive(*act) && (it_cat.flags & ActionCatalogItem::FLAGS_SPECIFIC)) {
                        ids.emplace_back(id);
                        auto item = Gio::MenuItem::create(it_cat.name.menu, "menu." + action_tool_id_to_string(id));
                        item->set_attribute_value("custom",
                                                  Glib::Variant<Glib::ustring>::create(action_tool_id_to_string(id)));
                        menu->append_item(item);
                    }
                }
            }
        }
    }
    if (mode == ContextMenuMode::CONSTRAIN && ids.size() == 1) {
        get_canvas().set_selection(m_context_menu_selection, false);
        trigger_action(ids.front().id);
        return;
    }
    else if (mode == ContextMenuMode::CONSTRAIN && ids.size() == 0) {
        auto item = Gio::MenuItem::create("No applicable constraints", "menu.invalid");
        menu->append_item(item);
    }
    if (meas_items.size() > 1) {
        auto measurement_submenu = Gio::Menu::create();
        for (auto it : meas_items) {
            measurement_submenu->append_item(it);
        }
        auto item = Gio::MenuItem::create("Measure", measurement_submenu);
        menu->append_item(item);
    }
    else if (meas_items.size() == 1) {
        menu->append_item(meas_items.front());
    }

    if (m_core.has_documents() && mode != ContextMenuMode::CONSTRAIN) {
        auto &doc = m_core.get_current_document();
        if (auto enp = point_from_selection(doc, m_context_menu_selection)) {
            auto &en = doc.get_entity(enp->entity);
            std::vector<const Constraint *> constraints;
            for (auto constraint : en.get_constraints(doc)) {
                if (enp->point == 0 || constraint->get_referenced_entities_and_points().contains(*enp))
                    constraints.push_back(constraint);
            }
            std::ranges::sort(constraints, [](auto a, auto b) { return a->get_type() < b->get_type(); });

            if (constraints.size()) {
                auto submenu = Gio::Menu::create();
                for (auto constraint : constraints) {
                    auto item = Gio::MenuItem::create(constraint->get_type_name(), "menu.remove_constraint");
                    item->set_action_and_target("menu.remove_constraint",
                                                Glib::Variant<std::string>::create(constraint->m_uuid));
                    submenu->append_item(item);
                }
                auto item = Gio::MenuItem::create("Remove constraint", submenu);
                menu->append_item(item);
            }
        }
    }

    const bool has_any_can_force_unset_workplane =
            std::ranges::any_of(ids, [](const auto &x) { return x.force_unset_workplane_tool != ToolID::NONE; });
    auto sg = Gtk::SizeGroup::create(Gtk::SizeGroup::Mode::HORIZONTAL);
    if (menu->get_n_items() != 0) {
        m_context_menu->set_menu_model(menu);
        for (const auto [id, equivalent_tool, can_preview, force_unset_workplane_tool, constraint_is_in_workplane] :
             ids) {
            auto button = Gtk::make_managed<Gtk::Button>();
            button->signal_clicked().connect([this, id] {
                m_context_menu->popdown();
                get_canvas().set_selection(m_context_menu_selection, false);
                trigger_action(id);
            });
            if (m_preferences.editor.preview_constraints && can_preview) {
                install_hover(*button, std::get<ToolID>(id));
            }
            button->add_css_class("context-menu-button");
            auto label = Gtk::make_managed<Gtk::Label>(action_catalog.at(id).name.menu);
            label->set_xalign(0);
            auto seqs = m_action_connections.at(id).key_sequences;
            if (equivalent_tool != ToolID::NONE) {
                auto eq_seqs = m_action_connections.at(equivalent_tool).key_sequences;
                seqs.insert(seqs.end(), eq_seqs.begin(), eq_seqs.end());
            }
            auto label2 = Gtk::make_managed<Gtk::Label>(key_sequences_to_string(seqs));
            label2->add_css_class("dim-label");
            label2->set_margin_start(8);
            auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL);
            box->append(*label);
            if (constraint_is_in_workplane) {
                auto icon = Gtk::make_managed<Gtk::Image>();
                icon->set_tooltip_text("Constraint applies in workplane");
                icon->set_from_icon_name("action-constrain-in-workplane-symbolic");
                box->append(*icon);
                icon->set_hexpand(true);
                icon->set_halign(Gtk::Align::START);
            }
            else {
                label->set_hexpand(true);
            }
            box->append(*label2);
            button->set_child(*box);

            button->set_has_frame(false);
            auto box2 = Gtk::make_managed<Gtk::Box>();
            box2->append(*button);
            if (force_unset_workplane_tool != ToolID::NONE) {
                auto button2 = Gtk::make_managed<Gtk::Button>("in 3D");
                button2->add_css_class("context-menu-button");
                button2->add_css_class("context-menu-button-3d");
                button2->set_has_frame(false);
                sg->add_widget(*button2);
                button2->signal_clicked().connect([this, force_unset_workplane_tool] {
                    m_context_menu->popdown();
                    get_canvas().set_selection(m_context_menu_selection, false);
                    tool_begin(force_unset_workplane_tool);
                });
                if (m_preferences.editor.preview_constraints && can_preview) {
                    install_hover(*button2, force_unset_workplane_tool);
                }


                box2->append(*button2);
            }
            else if (has_any_can_force_unset_workplane) {
                auto placeholder = Gtk::make_managed<Gtk::Label>();
                sg->add_widget(*placeholder);
                box2->append(*placeholder);
            }
            m_context_menu->add_child(*box2, action_tool_id_to_string(id));
        }
        m_context_menu->popup();
    }
    else if (mode == ContextMenuMode::CONSTRAIN) {
        m_context_menu->popup();
    }
}

void Editor::init_properties_notebook()
{
    m_properties_notebook = Gtk::make_managed<Gtk::Notebook>();
    m_properties_notebook->set_show_border(false);
    m_properties_notebook->set_tab_pos(Gtk::PositionType::BOTTOM);
    // The lower properties notebook is intentionally not attached to the
    // left pane.  The tree remains, but the Group/Constraints/Selection tabs
    // are no longer displayed there.
    {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
        m_group_editor_box = Gtk::make_managed<Gtk::Box>();
        m_group_editor_box->set_vexpand(true);
        box->append(*m_group_editor_box);
        auto label = Gtk::make_managed<Gtk::Label>("Commit pending");
        label->set_margin(3);
        m_group_commit_pending_revealer = Gtk::make_managed<Gtk::Revealer>();
        m_group_commit_pending_revealer->set_transition_type(Gtk::RevealerTransitionType::CROSSFADE);
        m_group_commit_pending_revealer->set_child(*label);
        box->append(*m_group_commit_pending_revealer);
        m_properties_notebook->append_page(*box, "Group");
    }
    m_core.signal_rebuilt().connect([this] {
        if (m_group_editor) {
            if (m_core.get_current_group() != m_group_editor->get_group())
                update_group_editor();
            else
                m_group_editor->reload();
        }
    });


    m_constraints_box = Gtk::make_managed<ConstraintsBox>(m_core);
    m_properties_notebook->append_page(*m_constraints_box, "Constraints");
    m_core.signal_rebuilt().connect(
            [this] { Glib::signal_idle().connect_once([this] { m_constraints_box->update(); }); });
    m_core.signal_documents_changed().connect([this] { m_constraints_box->update(); });
    m_constraints_box->signal_constraint_selected().connect([this](const UUID &uu) {
        SelectableRef sr{.type = SelectableRef::Type::CONSTRAINT, .item = uu};
        get_canvas().set_selection({sr}, true);
        get_canvas().set_selection_mode(SelectionMode::NORMAL);
    });
    m_constraints_box->signal_changed().connect([this] { CanvasUpdater canvas_updater{*this}; });

    {
        auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);

        m_selection_editor = Gtk::make_managed<SelectionEditor>(m_core, static_cast<IDocumentViewProvider &>(*this));
        m_selection_editor->signal_changed().connect(sigc::mem_fun(*this, &Editor::handle_commit_from_editor));
        m_selection_editor->signal_view_changed().connect([this] { CanvasUpdater canvas_updater{*this}; });
        m_selection_editor->set_vexpand(true);
        box->append(*m_selection_editor);
        auto label = Gtk::make_managed<Gtk::Label>("Commit pending");
        label->set_margin(3);
        m_selection_commit_pending_revealer = Gtk::make_managed<Gtk::Revealer>();
        m_selection_commit_pending_revealer->set_transition_type(Gtk::RevealerTransitionType::CROSSFADE);
        m_selection_commit_pending_revealer->set_child(*label);
        box->append(*m_selection_commit_pending_revealer);
        m_properties_notebook->append_page(*box, "Selection");
    }
    get_canvas().signal_selection_changed().connect(sigc::mem_fun(*this, &Editor::update_selection_editor));
}

void Editor::update_selection_editor()
{
    if (get_canvas().get_selection_mode() == SelectionMode::HOVER)
        m_selection_editor->set_selection({});
    else
        m_selection_editor->set_selection(get_canvas().get_selection());
}

void Editor::init_header_bar()
{
    {
        auto menu = Gio::Menu::create();
        auto actions = Gio::SimpleActionGroup::create();
        actions->add_action("sketch", [this] { trigger_action(ActionID::CREATE_GROUP_SKETCH); });
        actions->add_action("extrude", [this] { trigger_action(ActionID::CREATE_GROUP_EXTRUDE); });
        actions->add_action("revolve", [this] { trigger_action(ActionID::CREATE_GROUP_REVOLVE); });
        actions->add_action("sweep", [this] { trigger_action(ActionID::CREATE_GROUP_PIPE); });
        actions->add_action("loft", [this] { trigger_action(ActionID::CREATE_GROUP_LOFT); });
        actions->add_action("rectangular_pattern", [this] { trigger_action(ActionID::CREATE_GROUP_LINEAR_ARRAY); });
        actions->add_action("circular_pattern", [this] { trigger_action(ActionID::CREATE_GROUP_POLAR_ARRAY); });
        actions->add_action("mirror", [this] { trigger_action(ActionID::CREATE_GROUP_MIRROR_HORIZONTAL); });
        actions->add_action("rib", [this] { trigger_action(ActionID::CREATE_GROUP_EXTRUDE); });
        actions->add_action("web", [this] { trigger_action(ActionID::CREATE_GROUP_EXTRUDE); });
        actions->add_action("hole", [this] { trigger_action(ActionID::CREATE_GROUP_EXTRUDE); });
        actions->add_action("thread", [this] { trigger_action(ActionID::CREATE_GROUP_EXTRUDE); });
        actions->add_action("emboss", [this] { trigger_action(ActionID::CREATE_GROUP_EXTRUDE); });
        m_win.insert_action_group("ribbon_create_features", actions);
        menu->append("Sketch", "ribbon_create_features.sketch");
        menu->append("Extrude", "ribbon_create_features.extrude");
        menu->append("Revolve", "ribbon_create_features.revolve");
        menu->append("Sweep", "ribbon_create_features.sweep");
        menu->append("Loft", "ribbon_create_features.loft");
        menu->append("Rib", "ribbon_create_features.rib");
        menu->append("Web", "ribbon_create_features.web");
        menu->append("Hole", "ribbon_create_features.hole");
        menu->append("Thread", "ribbon_create_features.thread");
        menu->append("Emboss", "ribbon_create_features.emboss");
        menu->append("Rectangular Pattern", "ribbon_create_features.rectangular_pattern");
        menu->append("Circular Pattern", "ribbon_create_features.circular_pattern");
        menu->append("Mirror", "ribbon_create_features.mirror");
        auto popover = Gtk::make_managed<Gtk::PopoverMenu>(menu, Gtk::PopoverMenu::Flags::NESTED);
        m_win.get_ribbon_create_menu_button().set_popover(*popover);
    }
    {
        auto menu = Gio::Menu::create();
        auto actions = Gio::SimpleActionGroup::create();
        actions->add_action("line", [this] { trigger_action(ToolID::DRAW_CONTOUR); });
        actions->add_action("rectangle_2_point", [this] { trigger_action(ToolID::DRAW_RECTANGLE); });
        actions->add_action("circle", [this] { trigger_action(ToolID::DRAW_CIRCLE_2D); });
        actions->add_action("arc", [this] { trigger_action(ToolID::DRAW_ARC_3_POINT); });
        actions->add_action("polygon", [this] { trigger_action(ToolID::DRAW_REGULAR_POLYGON); });
        actions->add_action("spline", [this] { trigger_action(ToolID::DRAW_BEZIER_2D); });
        actions->add_action("point", [this] { trigger_action(ToolID::DRAW_POINT_2D); });
        actions->add_action("text", [this] { trigger_action(ToolID::DRAW_TEXT); });
        actions->add_action("sketch_dimension", [this] { trigger_action(ToolID::CONSTRAIN_DISTANCE); });
        actions->add_action("rectangle_3_point", [this] { trigger_action(ToolID::DRAW_RECTANGLE_3_POINT); });
        actions->add_action("rectangle_center", [this] { trigger_action(ToolID::DRAW_RECTANGLE_CENTER); });
        actions->add_action("slot", [this] { trigger_action(ToolID::DRAW_SLOT_CENTER_TO_CENTER); });
        actions->add_action("circle_2_point", [this] { trigger_action(ToolID::DRAW_CIRCLE_2_POINT); });
        actions->add_action("circle_3_point", [this] { trigger_action(ToolID::DRAW_CIRCLE_3_POINT); });
        actions->add_action("circle_2_tangent", [this] { trigger_action(ToolID::DRAW_CIRCLE_2_TANGENT); });
        actions->add_action("circle_3_tangent", [this] { trigger_action(ToolID::DRAW_CIRCLE_3_TANGENT); });
        actions->add_action("arc_center_point", [this] { trigger_action(ToolID::DRAW_ARC_CENTER_POINT); });
        actions->add_action("arc_tangent", [this] { trigger_action(ToolID::DRAW_ARC_TANGENT); });
        actions->add_action("polygon_inscribed", [this] { trigger_action(ToolID::DRAW_REGULAR_POLYGON); });
        actions->add_action("polygon_edge", [this] { trigger_action(ToolID::DRAW_REGULAR_POLYGON); });
        actions->add_action("slot_overall", [this] { trigger_action(ToolID::DRAW_SLOT_OVERALL); });
        actions->add_action("slot_center_point", [this] { trigger_action(ToolID::DRAW_SLOT_CENTER_POINT); });
        actions->add_action("slot_3_point_arc", [this] { trigger_action(ToolID::DRAW_SLOT_3_POINT_ARC); });
        actions->add_action("slot_center_point_arc", [this] { trigger_action(ToolID::DRAW_SLOT_CENTER_POINT_ARC); });
        actions->add_action("spline_control_point", [this] { trigger_action(ToolID::DRAW_BEZIER_2D); });
        actions->add_action("project", [this] { trigger_action(ToolID::PROJECT_SKETCH_GEOMETRY); });
        actions->add_action("intersect", [this] { trigger_action(ToolID::PROJECT_SKETCH_GEOMETRY); });
        actions->add_action("include_3d_geometry", [this] { trigger_action(ToolID::PROJECT_SKETCH_GEOMETRY); });
        actions->add_action("project_to_surface", [this] { trigger_action(ToolID::PROJECT_SKETCH_GEOMETRY); });
        actions->add_action("intersection_curve", [this] { trigger_action(ToolID::PROJECT_SKETCH_GEOMETRY); });
        actions->add_action("ellipse", [this] { trigger_action(ToolID::DRAW_ELLIPSE); });
        auto conic_curve = actions->add_action("conic_curve", [this] { trigger_action(ToolID::DRAW_CONIC_CURVE); });
        auto mirror = actions->add_action("mirror", [this] { trigger_action(ActionID::CREATE_GROUP_MIRROR_HORIZONTAL); });
        auto circular_pattern =
                actions->add_action("circular_pattern", [this] { trigger_action(ActionID::CREATE_GROUP_POLAR_ARRAY); });
        auto rectangular_pattern = actions->add_action(
                "rectangular_pattern", [this] { trigger_action(ActionID::CREATE_GROUP_LINEAR_ARRAY); });
        m_win.insert_action_group("ribbon_create", actions);
        menu->append("Line / Contour", "ribbon_create.line");
        auto rectangle_menu = Gio::Menu::create();
        rectangle_menu->append("2-Point Rectangle", "ribbon_create.rectangle_2_point");
        rectangle_menu->append("3-Point Rectangle", "ribbon_create.rectangle_3_point");
        rectangle_menu->append("Center Rectangle", "ribbon_create.rectangle_center");
        menu->append_submenu("Rectangle", rectangle_menu);
        auto circle_menu = Gio::Menu::create();
        circle_menu->append("Center Diameter Circle", "ribbon_create.circle");
        circle_menu->append("2-Point Circle", "ribbon_create.circle_2_point");
        circle_menu->append("3-Point Circle", "ribbon_create.circle_3_point");
        circle_menu->append("2-Tangent Circle", "ribbon_create.circle_2_tangent");
        circle_menu->append("3-Tangent Circle", "ribbon_create.circle_3_tangent");
        menu->append_submenu("Circle", circle_menu);
        auto arc_menu = Gio::Menu::create();
        arc_menu->append("3-Point Arc", "ribbon_create.arc");
        arc_menu->append("Center Point Arc", "ribbon_create.arc_center_point");
        arc_menu->append("Tangent Arc", "ribbon_create.arc_tangent");
        menu->append_submenu("Arc", arc_menu);
        auto polygon_menu = Gio::Menu::create();
        polygon_menu->append("Circumscribed Polygon", "ribbon_create.polygon");
        polygon_menu->append("Inscribed Polygon", "ribbon_create.polygon_inscribed");
        polygon_menu->append("Edge Polygon", "ribbon_create.polygon_edge");
        menu->append_submenu("Polygon", polygon_menu);
        menu->append("Ellipse", "ribbon_create.ellipse");
        auto slot_menu = Gio::Menu::create();
        slot_menu->append("Center to Center Slot", "ribbon_create.slot");
        slot_menu->append("Overall Slot", "ribbon_create.slot_overall");
        slot_menu->append("Center Point Slot", "ribbon_create.slot_center_point");
        slot_menu->append("3 Point Arc Slot", "ribbon_create.slot_3_point_arc");
        slot_menu->append("Center Point Arc Slot", "ribbon_create.slot_center_point_arc");
        menu->append_submenu("Slot", slot_menu);
        auto spline_menu = Gio::Menu::create();
        spline_menu->append("Fit Point Spline", "ribbon_create.spline");
        spline_menu->append("Control Point Spline", "ribbon_create.spline_control_point");
        menu->append_submenu("Spline", spline_menu);
        menu->append("Conic Curve", "ribbon_create.conic_curve");
        menu->append("Point", "ribbon_create.point");
        menu->append("Text", "ribbon_create.text");
        menu->append("Mirror", "ribbon_create.mirror");
        menu->append("Circular Pattern", "ribbon_create.circular_pattern");
        menu->append("Rectangular Pattern", "ribbon_create.rectangular_pattern");
        auto project_include_menu = Gio::Menu::create();
        project_include_menu->append("Project", "ribbon_create.project");
        project_include_menu->append("Intersect", "ribbon_create.intersect");
        project_include_menu->append("Include 3D Geometry", "ribbon_create.include_3d_geometry");
        project_include_menu->append("Project To Surface", "ribbon_create.project_to_surface");
        project_include_menu->append("Intersection Curve", "ribbon_create.intersection_curve");
        menu->append_submenu("Project/Include", project_include_menu);
        menu->append("Sketch Dimension", "ribbon_create.sketch_dimension");
        auto popover = Gtk::make_managed<Gtk::PopoverMenu>(menu, Gtk::PopoverMenu::Flags::NESTED);
        m_win.get_ribbon_sketch_create_menu_button().set_popover(*popover);
    }
    {
        auto menu = Gio::Menu::create();
        auto actions = Gio::SimpleActionGroup::create();
        actions->add_action("insert_image", [this] { trigger_action(ToolID::IMPORT_PICTURE); });
        actions->add_action("insert_step", [this] { trigger_action(ToolID::IMPORT_STEP); });
        actions->add_action("insert_dxf", [this] { trigger_action(ToolID::IMPORT_DXF); });
        actions->add_action("insert_mesh", [this] { trigger_action(ToolID::IMPORT_STL); });
        actions->add_action("insert_component", [this] { trigger_action(ToolID::LINK_DOCUMENT); });
        auto insert_svg = actions->add_action("insert_svg", [] {});
        auto derive = actions->add_action("derive", [] {});
        insert_svg->set_enabled(false);
        derive->set_enabled(false);
        m_win.insert_action_group("ribbon_insert", actions);
        menu->append("Insert Image", "ribbon_insert.insert_image");
        menu->append("Insert SVG", "ribbon_insert.insert_svg");
        menu->append("Insert Step", "ribbon_insert.insert_step");
        menu->append("Insert DXF", "ribbon_insert.insert_dxf");
        menu->append("Insert Mesh", "ribbon_insert.insert_mesh");
        menu->append("Insert Component", "ribbon_insert.insert_component");
        menu->append("Insert Derive", "ribbon_insert.derive");
        auto popover = Gtk::make_managed<Gtk::PopoverMenu>(menu, Gtk::PopoverMenu::Flags::NESTED);
        m_win.get_ribbon_insert_menu_button().set_popover(*popover);
    }

    attach_action_button(m_win.get_open_button(), ActionID::OPEN_DOCUMENT);
    attach_action_sensitive(m_win.get_open_menu_button(), ActionID::OPEN_DOCUMENT);
    attach_action_button(m_win.get_new_button(), ActionID::NEW_DOCUMENT);
    attach_action_button(m_win.get_save_button(), ActionID::SAVE);
    attach_action_button(m_win.get_save_as_button(), ActionID::SAVE_AS);

    // Attach Top Ribbon Bar Buttons
    attach_action_button(m_win.get_ribbon_btn_sketch(), ActionID::CREATE_GROUP_SKETCH);
    attach_action_button(m_win.get_ribbon_btn_extrude(), ActionID::CREATE_GROUP_EXTRUDE);
    attach_action_button(m_win.get_ribbon_btn_revolve(), ActionID::CREATE_GROUP_REVOLVE);
    attach_action_button(m_win.get_ribbon_btn_sweep(), ActionID::CREATE_GROUP_PIPE);
    attach_action_button(m_win.get_ribbon_btn_loft(), ActionID::CREATE_GROUP_LOFT);

    attach_action_button(m_win.get_ribbon_btn_fillet(), ActionID::CREATE_GROUP_FILLET);
    attach_action_button(m_win.get_ribbon_btn_chamfer(), ActionID::CREATE_GROUP_CHAMFER);
    attach_action_button(m_win.get_ribbon_btn_combine(), ActionID::CREATE_GROUP_SOLID_MODEL_OPERATION);
    attach_action_button(m_win.get_ribbon_btn_pattern(), ActionID::CREATE_GROUP_LINEAR_ARRAY);

    attach_action_button(m_win.get_ribbon_btn_line(), ToolID::DRAW_CONTOUR);
    attach_action_button(m_win.get_ribbon_btn_rect(), ToolID::DRAW_RECTANGLE);
    attach_action_button(m_win.get_ribbon_btn_circle(), ToolID::DRAW_CIRCLE_2D);
    attach_action_button(m_win.get_ribbon_btn_polygon(), ToolID::DRAW_REGULAR_POLYGON);
    attach_action_button(m_win.get_ribbon_btn_dimension_create(), ToolID::CONSTRAIN_DISTANCE);

    attach_action_button(m_win.get_ribbon_sketch_btn_fillet(), ToolID::SKETCH_FILLET);
    attach_action_button(m_win.get_ribbon_sketch_btn_chamfer(), ToolID::SKETCH_CHAMFER);
    attach_action_button(m_win.get_ribbon_body_btn_measure(), ToolID::MEASURE_DISTANCE);
    attach_action_button(m_win.get_ribbon_sketch_btn_measure(), ToolID::MEASURE_DISTANCE);
    m_win.get_finish_sketch_button().signal_clicked().connect([this] {
        if (m_extrude_editing)
            finish_extrusion();
        else
            finish_sketch();
    });
    update_sketch_mode_ui();

    {
        auto undo_button = create_action_button(ActionID::UNDO);
        undo_button->set_tooltip_text("Undo");
        undo_button->set_image_from_icon_name("edit-undo-symbolic");
        m_win.get_window_undo_redo_box().append(*undo_button);

        auto redo_button = create_action_button(ActionID::REDO);
        redo_button->set_tooltip_text("Redo");
        redo_button->set_image_from_icon_name("edit-redo-symbolic");
        m_win.get_window_undo_redo_box().append(*redo_button);
    }

    {
        auto top = Gio::Menu::create();

        top->append_item(Gio::MenuItem::create("Preferences", "app.preferences"));
        top->append_item(Gio::MenuItem::create("Logger", "app.logger"));
        top->append_item(Gio::MenuItem::create("About", "app.about"));

        m_win.get_hamburger_menu_button().set_menu_model(top);
    }
}

void Editor::update_view_hints()
{
    std::vector<std::string> hints;
    if (get_canvas().get_projection() == Canvas::Projection::PERSP)
        hints.push_back("persp.");
    {
        const auto cl = get_canvas().get_clipping_planes();
        if (cl.x.enabled || cl.y.enabled || cl.z.enabled) {
            std::string s = "clipped:";
            if (cl.x.enabled)
                s += "x";
            if (cl.y.enabled)
                s += "y";
            if (cl.z.enabled)
                s += "z";
            hints.push_back(s);
        }
    }
    if (m_selection_filter_window->is_active())
        hints.push_back("selection filtered");

    if (m_core.has_documents()) {
        auto &wv = get_current_workspace_view();

        if (wv.m_show_only_solid_models)
            hints.push_back("only solid models");
        if (wv.m_show_construction_entities_from_previous_groups)
            hints.push_back("prev. construction entities");
        if (wv.m_hide_irrelevant_workplanes)
            hints.push_back("no irrelevant workplanes");
        if (wv.m_curvature_comb_scale > 0)
            hints.push_back("curv. combs");
    }
    m_win.set_view_hints_label(hints);
}

void Editor::on_open_document(const ActionConnection &conn)
{
    auto dialog = Gtk::FileDialog::create();
    if (m_core.has_documents()) {
        if (m_core.get_current_idocument_info().has_path()) {
            dialog->set_initial_file(
                    Gio::File::create_for_path(path_to_string(m_core.get_current_idocument_info().get_path())));
        }
    }

    // Add filters, so that only certain file types can be selected:
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();

    auto filter_any = Gtk::FileFilter::create();
    filter_any->set_name("Dune 3D documents");
    filter_any->add_pattern("*.d3ddoc");
    filters->append(filter_any);

    dialog->set_filters(filters);

    // Show the dialog and wait for a user response:
    dialog->open(m_win, [this, dialog](const Glib::RefPtr<Gio::AsyncResult> &result) {
        try {
            auto file = dialog->open_finish(result);
            m_win.open_file_view(file);
            // Notice that this is a std::string, not a Glib::ustring.
            auto filename = file->get_path();
            std::cout << "File selected: " << filename << std::endl;
        }
        catch (const Gtk::DialogError &err) {
            // Can be thrown by dialog->open_finish(result).
            std::cout << "No file selected. " << err.what() << std::endl;
        }
        catch (const Glib::Error &err) {
            std::cout << "Unexpected exception. " << err.what() << std::endl;
        }
    });
}


void Editor::on_save_as(const ActionConnection &conn)
{
    auto dialog = Gtk::FileDialog::create();
    if (m_core.get_current_idocument_info().has_path()) {
        dialog->set_initial_file(
                Gio::File::create_for_path(path_to_string(m_core.get_current_idocument_info().get_path())));
    }

    // Add filters, so that only certain file types can be selected:
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();

    auto filter_any = Gtk::FileFilter::create();
    filter_any->set_name("Dune 3D documents");
    filter_any->add_pattern("*.d3ddoc");
    filters->append(filter_any);

    dialog->set_filters(filters);

    // Show the dialog and wait for a user response:
    dialog->save(m_win, [this, dialog](const Glib::RefPtr<Gio::AsyncResult> &result) {
        try {
            auto file = dialog->save_finish(result);
            // open_file_view(file);
            //  Notice that this is a std::string, not a Glib::ustring.
            auto filename = path_from_string(append_suffix_if_required(file->get_path(), ".d3ddoc"));
            // std::cout << "File selected: " << filename << std::endl;
            m_win.get_app().add_recent_item(filename);
            m_core.save_as(filename);
            save_workspace_view(m_core.get_current_idocument_info().get_uuid());
            m_workspace_browser->update_documents(get_current_document_views());
            update_version_info();
            update_title();
            if (m_after_save_cb)
                m_after_save_cb();
            m_after_save_cb = nullptr;
        }
        catch (const Gtk::DialogError &err) {
            // Can be thrown by dialog->open_finish(result).
            std::cout << "No file selected. " << err.what() << std::endl;
        }
        catch (const Glib::Error &err) {
            std::cout << "Unexpected exception. " << err.what() << std::endl;
        }
    });
}


void Editor::init_tool_popover()
{
    m_tool_popover = Gtk::make_managed<ToolPopover>();
    m_tool_popover->set_parent(get_canvas());
    m_tool_popover->signal_action_activated().connect([this](ActionToolID action_id) { trigger_action(action_id); });


    connect_action({ActionID::POPOVER}, [this](const auto &a) {
        Gdk::Rectangle rect;
        rect.set_x(m_last_x);
        rect.set_y(m_last_y);

        m_tool_popover->set_pointing_to(rect);

        this->update_action_sensitivity();
        std::map<ActionToolID, bool> can_begin;
        auto sel = get_canvas().get_selection();
        for (const auto &[id, it] : action_catalog) {
            if (std::holds_alternative<ToolID>(id)) {
                bool r = m_core.tool_can_begin(std::get<ToolID>(id), sel).get_can_begin();
                can_begin[id] = r;
            }
            else {
                can_begin[id] = this->get_action_sensitive(std::get<ActionID>(id));
            }
        }
        m_tool_popover->set_can_begin(can_begin);

        m_tool_popover->popup();
    });
}

Canvas &Editor::get_canvas()
{
    return m_win.get_canvas();
}

const Canvas &Editor::get_canvas() const
{
    return m_win.get_canvas();
}


void Editor::show_save_dialog(const std::string &doc_name, std::function<void()> save_cb,
                              std::function<void()> no_save_cb)
{
    auto dialog = Gtk::AlertDialog::create("Save changes to document \"" + doc_name + "\" before closing?");
    dialog->set_detail(
            "If you don't save, all your changes will be permanently "
            "lost.");
    dialog->set_buttons({"Cancel", "Close without saving", "Save"});
    dialog->set_cancel_button(0);
    dialog->set_default_button(0);
    dialog->choose(m_win, [dialog, save_cb, no_save_cb](Glib::RefPtr<Gio::AsyncResult> &result) {
        auto btn = dialog->choose_finish(result);
        if (btn == 1) {
            if (no_save_cb)
                no_save_cb();
        }
        else if (btn == 2)
            if (save_cb)
                save_cb();
    });
}

void Editor::close_document(const UUID &doc_uu, std::function<void()> save_cb, std::function<void()> no_save_cb)
{
    const auto &doci = m_core.get_idocument_info(doc_uu);
    if (doci.get_needs_save()) {
        show_save_dialog(
                doci.get_basename(),
                [this, doc_uu, save_cb, no_save_cb] {
                    m_after_save_cb = [this, doc_uu, save_cb] {
                        do_close_document(doc_uu);
                        if (save_cb)
                            save_cb();
                    };
                    trigger_action(ActionID::SAVE);
                },
                [this, doc_uu, no_save_cb] {
                    do_close_document(doc_uu);
                    if (no_save_cb)
                        no_save_cb();
                });
    }
    else {
        do_close_document(doc_uu);
    }
}

void Editor::do_close_document(const UUID &doc_uu)
{
    if (m_core.get_current_idocument_info().get_uuid() == doc_uu)
        force_end_tool();
    m_core.close_document(doc_uu);
    remove_workspace_browser(doc_uu);
    auto_close_workspace_views();
}

void Editor::update_group_editor()
{
    if (m_group_editor) {
        if (m_delayed_commit_connection.connected()) {
            commit_from_editor();
        }
        m_group_editor_box->remove(*m_group_editor);
        m_group_editor = nullptr;
    }
    if (!m_core.has_documents())
        return;
    m_group_editor = GroupEditor::create(m_core, m_core.get_current_group());
    m_group_editor->signal_changed().connect(sigc::mem_fun(*this, &Editor::handle_commit_from_editor));
    m_group_editor->signal_trigger_action().connect([this](auto act) { trigger_action(act); });
    m_group_editor_box->append(*m_group_editor);
}

void Editor::handle_commit_from_editor(CommitMode mode)
{
    if (mode == CommitMode::DELAYED) {
        m_core.get_current_document().update_pending(m_core.get_current_group());
        m_delayed_commit_connection.disconnect(); // stop old timer
        m_delayed_commit_connection = Glib::signal_timeout().connect(
                [this] {
                    commit_from_editor();
                    return false;
                },
                1000);
        m_group_commit_pending_revealer->set_reveal_child(true);
        m_selection_commit_pending_revealer->set_reveal_child(true);
    }
    else if (mode == CommitMode::IMMEDIATE
             || (mode == CommitMode::EXECUTE_DELAYED && m_delayed_commit_connection.connected())) {
        commit_from_editor();
    }
    m_core.set_needs_save();
    canvas_update_keep_selection();
}

void Editor::commit_from_editor()
{
    m_delayed_commit_connection.disconnect();
    m_group_commit_pending_revealer->set_reveal_child(false);
    m_selection_commit_pending_revealer->set_reveal_child(false);
    m_core.rebuild("group/selection edited");
}

void Editor::update_workplane_label()
{
    if (!m_core.has_documents()) {
        m_win.set_workplane_label("No documents");
        return;
    }
    auto wrkpl_uu = m_core.get_current_workplane();
    if (!wrkpl_uu) {
        m_win.set_workplane_label("No Workplane");
    }
    else {
        auto &wrkpl = m_core.get_current_document().get_entity<EntityWorkplane>(wrkpl_uu);
        auto &wrkpl_group = m_core.get_current_document().get_group<Group>(wrkpl.m_group);
        std::string s = "Workplane ";
        if (wrkpl.m_name.size())
            s += wrkpl.m_name + " ";
        s += "in group " + wrkpl_group.m_name;
        m_win.set_workplane_label(s);
    }
}

KeyMatchResult Editor::keys_match(const KeySequence &keys) const
{
    return key_sequence_match(m_keys_current, keys);
}

void Editor::apply_preferences()
{
    CanvasUpdater canvas_updater{*this};

    for (auto &[id, conn] : m_action_connections) {
        auto &act = action_catalog.at(id);
        if (!(act.flags & ActionCatalogItem::FLAGS_NO_PREFERENCES) && m_preferences.key_sequences.keys.count(id)) {
            conn.key_sequences = m_preferences.key_sequences.keys.at(id);
        }
    }
    m_in_tool_key_sequeces_preferences = m_preferences.in_tool_key_sequences;
    m_in_tool_key_sequeces_preferences.keys.erase(InToolActionID::LMB);
    m_in_tool_key_sequeces_preferences.keys.erase(InToolActionID::RMB);

    {
        const auto mod0 = static_cast<Gdk::ModifierType>(0);

        m_in_tool_key_sequeces_preferences.keys[InToolActionID::CANCEL] = {{{GDK_KEY_Escape, mod0}}};
        m_in_tool_key_sequeces_preferences.keys[InToolActionID::COMMIT] = {{{GDK_KEY_Return, mod0}},
                                                                           {{GDK_KEY_KP_Enter, mod0}}};
    }

    for (const auto &[id, it] : m_action_connections) {
        std::string tip = action_catalog.at(id).name.full;
        if (it.key_sequences.size()) {
            m_tool_popover->set_key_sequences(id, it.key_sequences);
            tip += " (" + key_sequences_to_string(it.key_sequences) + ")";
        }
        if (m_action_bar_buttons.contains(id)) {
            auto &button = *m_action_bar_buttons.at(id);
            button.set_tooltip_text(tip);
        }
    }

    auto dark = Gtk::Settings::get_default()->property_gtk_application_prefer_dark_theme().get_value();
    if (dark != m_preferences.canvas.dark_theme)
        Gtk::Settings::get_default()->property_gtk_application_prefer_dark_theme().set_value(
                m_preferences.canvas.dark_theme);
    dark = Gtk::Settings::get_default()->property_gtk_application_prefer_dark_theme().get_value();
    switch (m_preferences.canvas.theme_variant) {
    case CanvasPreferences::ThemeVariant::AUTO:
        break;
    case CanvasPreferences::ThemeVariant::DARK:
        dark = true;
        break;
    case CanvasPreferences::ThemeVariant::LIGHT:
        dark = false;
        break;
    }
    if (color_themes.contains(m_preferences.canvas.theme)) {
        Appearance appearance = m_preferences.canvas.appearance;
        appearance.colors = color_themes.at(m_preferences.canvas.theme).get(dark);
        get_canvas().set_appearance(appearance);
    }
    else {
        get_canvas().set_appearance(m_preferences.canvas.appearance);
    }
    get_canvas().set_enable_animations(m_preferences.canvas.enable_animations);
    get_canvas().set_zoom_to_cursor(m_preferences.canvas.zoom_to_cursor);
    get_canvas().set_rotation_scheme(m_preferences.canvas.rotation_scheme);

    m_win.tool_bar_set_vertical(m_preferences.tool_bar.vertical_layout);
    update_action_bar_visibility();
    update_error_overlay();

    /*
        key_sequence_dialog->clear();
        for (const auto &it : action_connections) {
            if (it.second.key_sequences.size()) {
                key_sequence_dialog->add_sequence(it.second.key_sequences, action_catalog.at(it.first).name);
                tool_popover->set_key_sequences(it.first, it.second.key_sequences);
            }
        }
        preferences_apply_to_canvas(canvas, preferences);
        for (auto it : action_buttons) {
            it->update_key_sequences();
            it->set_keep_primary_action(!preferences.action_bar.remember);
        }
        main_window->set_use_action_bar(preferences.action_bar.enable);
        m_core.set_history_max(preferences.undo_redo.max_depth);
        m_core.set_history_never_forgets(preferences.undo_redo.never_forgets);
        selection_history_manager.set_never_forgets(preferences.undo_redo.never_forgets);
        preferences_apply_appearance(preferences);
        */
}

void Editor::update_error_overlay()
{
    if (m_core.has_documents()) {
        auto &doc = m_core.get_current_document();
        auto &group = doc.get_group(m_core.get_current_group());
        get_canvas().set_show_error_overlay(m_preferences.canvas.error_overlay
                                            && group.m_solve_result != SolveResult::OKAY);
    }
    else {
        get_canvas().set_show_error_overlay(false);
    }
}

void Editor::render_document(const IDocumentInfo &doc)
{
    auto &doc_view = get_current_document_views()[doc.get_uuid()];
    if (!doc_view.m_document_is_visible && (doc.get_uuid() != m_core.get_current_idocument_info().get_uuid()))
        return;
    std::optional<SelectableRef> sr;
    if (doc.get_uuid() != m_core.get_current_idocument_info().get_uuid()) {
        sr = SelectableRef{SelectableRef::Type::DOCUMENT, doc.get_uuid()};
    }
    Renderer renderer(get_canvas(), m_core);
    renderer.m_solid_model_edge_select_mode = m_solid_model_edge_select_mode;
    renderer.m_connect_curvature_comb = m_preferences.canvas.connect_curvature_combs;
    renderer.m_first_group = m_update_groups_after;
    renderer.m_render_sketch_plane_selector = m_selecting_sketch_plane
                                               && doc.get_uuid() == m_core.get_current_idocument_info().get_uuid();
    renderer.m_render_sketch_grid = m_sketch_editing
                                    && doc.get_uuid() == m_core.get_current_idocument_info().get_uuid();
    if (renderer.m_render_sketch_grid && doc.get_uuid() == m_core.get_current_idocument_info().get_uuid()) {
        if (const auto *sketch = dynamic_cast<const GroupSketch *>
                                        (&doc.get_document().get_group(doc.get_current_group()));
            sketch && sketch->m_attached_to_face)
            renderer.m_render_attached_sketch_body_transparent = true;
    }
    renderer.m_render_extrusion_editor = m_extrude_editing
                                         && doc.get_uuid() == m_core.get_current_idocument_info().get_uuid();
    renderer.m_show_dimension_points = m_core.get_tool_id() == ToolID::CONSTRAIN_DISTANCE
                                      && doc.get_uuid() == m_core.get_current_idocument_info().get_uuid();
    for (const auto &selection : get_canvas().get_selection()) {
        Logger::log_debug(std::format("render selection type={} item={} point={}",
                                      static_cast<int>(selection.type), static_cast<std::string>(selection.item),
                                      selection.point),
                          Logger::Domain::CANVAS);
        if (selection.type == SelectableRef::Type::SKETCH_PROFILE) {
            renderer.m_selected_sketch_profile_group = selection.item;
            renderer.m_selected_sketch_profiles.insert(selection.point);
        }
    }
    renderer.m_sketch_plane_grid = m_sketch_plane_grid;
    renderer.m_sketch_grid_offset = m_sketch_grid_offset;
    if (renderer.m_render_sketch_plane_selector) {
        if (auto hover = get_canvas().get_hover_selection(); hover && hover->type == SelectableRef::Type::ENTITY)
            renderer.m_sketch_plane_hovered = hover->item;
    }

    if (doc.get_uuid() == m_core.get_current_idocument_info().get_uuid())
        renderer.add_constraint_icons(m_constraint_tip_pos, m_constraint_tip_vec, m_constraint_tip_icons);

    try {
        renderer.render(doc.get_document(), doc.get_current_group(), doc_view,
                        m_workspace_views.at(m_current_workspace_view), doc.get_dirname(), sr);
    }
    catch (const std::exception &ex) {
        Logger::log_critical("exception rendering document " + doc.get_basename(), Logger::Domain::RENDERER, ex.what());
    }
}
void Editor::canvas_update()
{
    if (m_extrude_editing && m_core.has_documents()
        && m_core.get_current_document().get_group(m_core.get_current_group()).get_type() == Group::Type::EXTRUDE) {
        auto &doc = m_core.get_current_document();
        const auto &group = doc.get_group<GroupExtrude>(m_core.get_current_group());
        const auto &workplane = doc.get_entity<EntityWorkplane>(group.m_wrkpl);
        const auto base = workplane.m_origin;
        const auto tip = base + group.m_dvec;
        if (!m_win.rectangle_dimensions_visible())
            m_win.show_extrude_dimension(glm::length(group.m_dvec));
        m_win.position_extrude_dimension(get_canvas().project_to_window(base), get_canvas().project_to_window(tip));
    }
    else if (m_rectangle_dimensions_origin)
        m_win.position_rectangle_dimensions(
                get_canvas().project_to_window(*m_rectangle_dimensions_origin),
                get_canvas().project_to_window(*m_rectangle_dimensions_x_min),
                get_canvas().project_to_window(*m_rectangle_dimensions_x_max),
                get_canvas().project_to_window(*m_rectangle_dimensions_y_min),
                get_canvas().project_to_window(*m_rectangle_dimensions_y_max), m_rectangle_dimensions_negative_x,
                m_rectangle_dimensions_negative_y);
    auto docs = m_core.get_documents();
    auto hover_sel = get_canvas().get_hover_selection();
    if (m_update_groups_after == UUID()) {
        get_canvas().clear();

        get_canvas().set_chunk(0);
        for (const auto doc : docs) {
            if (doc->get_uuid() != m_core.get_current_idocument_info().get_uuid())
                render_document(*doc);
        }
    }
    else {
        get_canvas().clear_chunks(
                Renderer::get_chunk_from_group(m_core.get_current_document().get_group(m_update_groups_after)));
    }

    if (m_core.has_documents())
        render_document(m_core.get_current_idocument_info());

    get_canvas().set_hover_selection(hover_sel);
    update_error_overlay();
    get_canvas().request_push();
    m_update_groups_after = UUID();
}

void Editor::canvas_update_keep_selection()
{
    auto sel = get_canvas().get_selection();
    canvas_update();
    get_canvas().set_selection(sel, false);
}

void Editor::enable_hover_selection(bool enable)
{
    get_canvas().set_selection_mode(enable ? SelectionMode::HOVER_ONLY : SelectionMode::NONE);
}

std::optional<SelectableRef> Editor::get_hover_selection() const
{
    return get_canvas().get_hover_selection();
}

glm::dvec3 Editor::get_cursor_pos() const
{
    return get_canvas().get_cursor_pos();
}

glm::vec3 Editor::get_cam_normal() const
{
    return get_canvas().get_cam_normal();
}

glm::quat Editor::get_cam_quat() const
{
    return get_canvas().get_cam_quat();
}

glm::dvec3 Editor::get_cursor_pos_for_plane(glm::dvec3 origin, glm::dvec3 normal) const
{
    return get_canvas().get_cursor_pos_for_plane(origin, normal);
}

glm::dvec3 Editor::get_cursor_pos_for_workplane(const EntityWorkplane &workplane) const
{
    const auto cursor = get_canvas().get_cursor_pos_for_plane(workplane.m_origin, workplane.get_normal_vector());
    if (!m_sketch_editing)
        return cursor;

    // Keep this in sync with Renderer::draw_sketch_grid.  The minor spacing
    // is the snap spacing; major intersections are included automatically.
    constexpr double default_camera_distance = 200.0;
    const double zoom_ratio = std::max(get_canvas().get_cam_distance() / default_camera_distance, 1e-12);
    const int zoom_steps = static_cast<int>(std::llround(std::log(zoom_ratio) / std::log(1.15)));
    int grid_index = 13;
    constexpr std::array<int, 8> zoom_out_thresholds = {10, 20, 33, 43, 53, 66, 76, 86};
    constexpr std::array<int, 10> zoom_in_thresholds = {1, 19, 28, 28, 100, 100, 100, 100, 100, 100};
    if (zoom_steps >= 0) {
        for (const auto threshold : zoom_out_thresholds) {
            if (zoom_steps >= threshold)
                grid_index++;
        }
    }
    else {
        for (const auto threshold : zoom_in_thresholds) {
            if (-zoom_steps >= threshold)
                grid_index--;
        }
    }
    constexpr std::array<double, 21> grid_intervals = {
            0.0001, 0.0005, 0.0025, 0.005, 0.01, 0.05, 0.25, 0.5, 1.0, 2.5, 5.0,
            12.5,   25.0,   50.0,   100.0, 250.0, 500.0, 1000.0, 2500.0, 5000.0, 10000.0};
    grid_index = std::max(grid_index, 9);
    const double label_spacing = grid_intervals.at(static_cast<size_t>(grid_index));
    double major_spacing = label_spacing;
    if (label_spacing == 25.0 && -zoom_steps >= 6)
        major_spacing = 5.0;
    else if (label_spacing == 12.5)
        major_spacing = 2.5;
    const double minor_spacing = major_spacing / 5.0;

    const auto point = workplane.project(cursor);
    const auto cursor_window = get_canvas().project_to_window(cursor);
    constexpr double snap_radius = 8.0;
    double best_distance = snap_radius;
    std::optional<glm::dvec3> best_snap;
    const auto consider_snap = [&](const glm::dvec3 &candidate) {
        const auto candidate_window = get_canvas().project_to_window(candidate);
        const auto distance = glm::length(candidate_window - cursor_window);
        if (distance <= best_distance) {
            best_distance = distance;
            best_snap = candidate;
        }
    };

    const glm::dvec2 snapped_point{std::round(point.x / minor_spacing) * minor_spacing,
                                   std::round(point.y / minor_spacing) * minor_spacing};
    if (snapped_point.x >= -300.0 && snapped_point.x <= 300.0 && snapped_point.y >= -300.0
        && snapped_point.y <= 300.0)
        consider_snap(workplane.transform(snapped_point));

    // A face-attached sketch should also snap to the supporting body's
    // corners.  The selected face is represented by the sketch workplane, so
    // use solid-model vertices that lie on that plane.  This keeps snapping
    // local to the face without changing the separate reference-plane picker.
    if (m_core.has_documents()) {
        const auto &doc = m_core.get_current_last_document();
        auto &current_group = doc.get_group(m_core.get_current_group());
        const auto *sketch = dynamic_cast<const GroupSketch *>(&current_group);
        if (sketch && sketch->m_attached_to_face) {
            const auto &body = current_group.find_body(doc).group;
            const auto *solid_group = dynamic_cast<const IGroupSolidModel *>(&body);
            if (solid_group && solid_group->get_solid_model()) {
                const auto plane_normal = workplane.get_normal_vector();
                for (const auto &face : solid_group->get_solid_model()->m_faces) {
                    for (const auto &vertex : face.vertices) {
                        const glm::dvec3 candidate{vertex.x, vertex.y, vertex.z};
                        if (std::abs(glm::dot(candidate - workplane.m_origin, plane_normal)) > 1e-4)
                            continue;
                        consider_snap(candidate);
                    }
                }
            }
        }
    }

    return best_snap.value_or(cursor);
}

void Editor::show_rectangle_dimensions(double width, double height)
{
    m_win.show_rectangle_dimensions(width, height);
}
void Editor::update_rectangle_dimensions(double width, double height)
{
    m_win.update_rectangle_dimensions(width, height);
}
void Editor::show_extrude_dimension(double height)
{
    m_win.show_extrude_dimension(height);
}
void Editor::update_extrude_dimension(double height)
{
    if (!m_extrude_editing || !m_core.has_documents() || height < 0 || height > 1e6)
        return;
    auto &doc = m_core.get_current_document();
    auto &group = doc.get_group<GroupExtrude>(m_core.get_current_group());
    auto direction = glm::length(group.m_dvec) > 1e-9 ? glm::normalize(group.m_dvec)
                                                       : doc.get_entity<EntityWorkplane>(group.m_wrkpl).get_normal_vector();
    group.m_dvec = direction * height;
    if (doc.get_groups().contains(group.m_source_group)) {
        const auto *sketch = dynamic_cast<const GroupSketch *>(&doc.get_group(group.m_source_group));
        if (sketch && sketch->m_attached_to_face) {
            const auto normal = doc.get_entity<EntityWorkplane>(group.m_wrkpl).get_normal_vector();
            group.m_operation = glm::dot(group.m_dvec, normal) < 0
                                         ? IGroupSolidModel::Operation::DIFFERENCE
                                         : IGroupSolidModel::Operation::UNION;
        }
    }
    doc.set_group_generate_pending(group.m_uuid);
    doc.update_pending();
    canvas_update_keep_selection();
}
void Editor::accept_extrude_dimension()
{
    finish_extrusion();
}
void Editor::hide_extrude_dimension()
{
    m_win.hide_extrude_dimension();
}
void Editor::position_extrude_dimension(glm::dvec3 base, glm::dvec3 tip)
{
    m_win.position_extrude_dimension(get_canvas().project_to_window(base), get_canvas().project_to_window(tip));
}
void Editor::hide_rectangle_dimensions()
{
    m_win.hide_rectangle_dimensions();
    m_rectangle_dimensions_origin.reset();
    m_rectangle_dimensions_x_min.reset();
    m_rectangle_dimensions_x_max.reset();
    m_rectangle_dimensions_y_min.reset();
    m_rectangle_dimensions_y_max.reset();
}

void Editor::show_circle_dimension(double diameter)
{
    m_win.show_circle_dimension(diameter);
}
void Editor::update_circle_dimension(double diameter)
{
    m_win.update_circle_dimension(diameter);
}
void Editor::hide_circle_dimension()
{
    m_win.hide_circle_dimension();
}
void Editor::position_circle_dimension(glm::dvec3 center, glm::dvec3 left, glm::dvec3 right)
{
    m_win.position_circle_dimension(get_canvas().project_to_window(center), get_canvas().project_to_window(left),
                                    get_canvas().project_to_window(right));
}

void Editor::position_rectangle_dimensions(glm::dvec3 origin, glm::dvec3 x_min, glm::dvec3 x_max,
                                           glm::dvec3 y_min, glm::dvec3 y_max, bool negative_x, bool negative_y)
{
    m_rectangle_dimensions_origin = origin;
    m_rectangle_dimensions_x_min = x_min;
    m_rectangle_dimensions_x_max = x_max;
    m_rectangle_dimensions_y_min = y_min;
    m_rectangle_dimensions_y_max = y_max;
    m_rectangle_dimensions_negative_x = negative_x;
    m_rectangle_dimensions_negative_y = negative_y;
    m_win.position_rectangle_dimensions(get_canvas().project_to_window(origin), get_canvas().project_to_window(x_min),
                                        get_canvas().project_to_window(x_max), get_canvas().project_to_window(y_min),
                                        get_canvas().project_to_window(y_max), negative_x, negative_y);
}

void Editor::accept_rectangle_dimensions()
{
    if (m_core.get_tool_id() != ToolID::DRAW_RECTANGLE)
        return;
    ToolArgs args;
    args.type = ToolEventType::ACTION;
    args.action = InToolActionID::LMB;
    ToolResponse response = m_core.tool_update(args);
    tool_process(response);
}

void Editor::accept_circle_dimension()
{
    if (m_core.get_tool_id() != ToolID::DRAW_CIRCLE_2D && m_core.get_tool_id() != ToolID::SKETCH_FILLET)
        return;
    ToolArgs args;
    args.type = ToolEventType::ACTION;
    args.action = InToolActionID::LMB;
    ToolResponse response = m_core.tool_update(args);
    tool_process(response);
}

void Editor::set_canvas_selection_mode(SelectionMode mode)
{
    m_last_selection_mode = mode;
}

void Editor::handle_cursor_move()
{
    if (m_extrude_dragging) {
        auto &doc = m_core.get_current_document();
        auto &group = doc.get_group<GroupExtrude>(m_extrude_drag_group);
        const auto &workplane = doc.get_entity<EntityWorkplane>(group.m_wrkpl);
        const auto cursor_on_screen_plane = get_canvas().get_cursor_pos_for_plane(
                workplane.m_origin, get_canvas().get_cam_normal());
        const auto distance = glm::dot(cursor_on_screen_plane - workplane.m_origin, m_extrude_drag_direction);
        group.m_dvec = m_extrude_drag_direction * (m_extrude_initial_length + distance - m_extrude_drag_start);
        if (doc.get_groups().contains(group.m_source_group)) {
            if (const auto *sketch = dynamic_cast<const GroupSketch *>(&doc.get_group(group.m_source_group));
                sketch && sketch->m_attached_to_face) {
                const auto normal = workplane.get_normal_vector();
                group.m_operation = glm::dot(group.m_dvec, normal) < 0
                                             ? IGroupSolidModel::Operation::DIFFERENCE
                                             : IGroupSolidModel::Operation::UNION;
            }
        }
        doc.set_group_generate_pending(group.m_uuid);
        doc.update_pending();
        m_extrude_drag_changed = true;
        m_win.update_extrude_dimension(glm::length(group.m_dvec));
        canvas_update_keep_selection();
        return;
    }
    if (m_core.tool_is_active()) {
        ToolArgs args;
        args.type = ToolEventType::MOVE;
        ToolResponse r = m_core.tool_update(args);
        tool_process(r);
    }
    else {
        if (m_drag_tool == ToolID::NONE)
            return;
        if (m_selection_for_drag.size() == 0)
            return;
        if (get_canvas().get_is_long_click())
            return;
        auto pos = get_canvas().get_cursor_pos_win();
        auto delta = pos - m_cursor_pos_win_drag_begin;
        if (glm::length(delta) > 10) {
            ToolArgs args;
            args.selection = m_selection_for_drag;
            m_last_selection_mode = get_canvas().get_selection_mode();
            get_canvas().set_selection_mode(SelectionMode::NONE);
            ToolResponse r = m_core.tool_begin(m_drag_tool, args, true);
            tool_process(r);

            m_selection_for_drag.clear();
            m_drag_tool = ToolID::NONE;
        }
    }
}

void Editor::handle_view_changed()
{
    if (!m_core.tool_is_active())
        return;
    if (!m_core.tool_handles_view_changed())
        return;

    ToolArgs args;
    args.type = ToolEventType::VIEW_CHANGED;
    ToolResponse r = m_core.tool_update(args);
    tool_process(r);
}

void Editor::handle_click(unsigned int button, unsigned int n)
{
    const bool is_doubleclick = n == 2;

    if (m_selecting_sketch_plane && button == 1) {
        if (auto hover_sel = get_canvas().get_hover_selection()) {
            if (hover_sel->type == SelectableRef::Type::ENTITY)
                finish_sketch_plane_selection(hover_sel->item);
            else if (hover_sel->type == SelectableRef::Type::SOLID_MODEL_FACE)
                finish_sketch_face_selection(hover_sel->item, hover_sel->point);
        }
        return;
    }

    if (m_extrude_editing && button == 1) {
        if (auto hover_sel = get_canvas().get_hover_selection(); hover_sel
            && hover_sel->type == SelectableRef::Type::EXTRUSION_HANDLE) {
            auto &doc = m_core.get_current_document();
            auto &group = doc.get_group<GroupExtrude>(hover_sel->item);
            const auto &workplane = doc.get_entity<EntityWorkplane>(group.m_wrkpl);
            m_extrude_drag_group = group.m_uuid;
            m_extrude_drag_direction = glm::normalize(group.m_dvec);
            m_extrude_initial_length = glm::length(group.m_dvec);
            const auto cursor_on_screen_plane = get_canvas().get_cursor_pos_for_plane(
                    workplane.m_origin, get_canvas().get_cam_normal());
            m_extrude_drag_start = glm::dot(cursor_on_screen_plane - workplane.m_origin,
                                            m_extrude_drag_direction);
            m_extrude_dragging = true;
            m_extrude_drag_changed = false;
            get_canvas().inhibit_drag_selection();
        }
        return;
    }

    // A viewed sketch is read-only. Keep its geometry available for the
    // double-click action that enters edit mode, but do not start a move drag
    // from its points or edges while it is not being edited.
    if (button == 1 && n == 1 && !m_sketch_editing && m_core.has_documents()) {
        if (auto hover_sel = get_canvas().get_hover_selection(); hover_sel
            && hover_sel->type == SelectableRef::Type::ENTITY) {
            auto &doc = m_core.get_current_document();
            const auto *entity = doc.get_entity_ptr(hover_sel->item);
            if (entity && doc.get_group(entity->m_group).get_type() == Group::Type::SKETCH)
                return;
        }
    }

    if (m_core.tool_is_active()) {
        // nop
    }
    else if (is_doubleclick && button == 1) {
        auto sel = get_canvas().get_hover_selection();
        if (sel) {
            if (auto action = get_doubleclick_action(*sel)) {
                get_canvas().set_selection({*sel}, false);
                get_canvas().inhibit_drag_selection();
                trigger_action(*action);
            }
        }
    }
    else if (button == 1) {
        auto hover_sel = get_canvas().get_hover_selection();
        if (!hover_sel)
            return;

        // Sketch geometry is edited through explicit sketch tools and
        // constraints. Do not start the generic move-drag when the sketch is
        // being edited; this prevents a line from being grabbed and moved by
        // an ordinary click-drag.
        if (m_sketch_editing && hover_sel->type != SelectableRef::Type::CONSTRAINT)
            return;

        auto sel = get_canvas().get_selection();
        if (!sel.contains(hover_sel.value()))
            sel = {*hover_sel};

        m_drag_tool = get_tool_for_drag_move(false, sel);
        if (m_drag_tool != ToolID::NONE && m_core.tool_can_begin(m_drag_tool, sel).get_can_begin()) {
            get_canvas().inhibit_drag_selection();
            m_cursor_pos_win_drag_begin = get_canvas().get_cursor_pos_win();
            m_selection_for_drag = sel;
        }
    }
}

ToolID Editor::get_tool_for_drag_move(bool ctrl, const std::set<SelectableRef> &sel)
{
    return ToolID::MOVE;
}

void Editor::reset_key_hint_label()
{
    const auto act = ActionID::POPOVER;
    if (m_action_connections.count(act)) {
        if (m_action_connections.at(act).key_sequences.size()) {
            const auto keys = key_sequence_to_string(m_action_connections.at(act).key_sequences.front());
            m_win.set_key_hint_label_text("> " + keys + " for menu");
            return;
        }
    }
    m_win.set_key_hint_label_text(">");
}

void Editor::tool_bar_clear_actions()
{
    m_win.tool_bar_clear_actions();
    m_in_tool_action_label_infos.clear();
}

void Editor::tool_bar_set_actions(const std::vector<ActionLabelInfo> &labels)
{
    if (m_in_tool_action_label_infos != labels) {
        tool_bar_clear_actions();
        for (const auto &it : labels) {
            tool_bar_append_action(it.action1, it.action2, it.action3, it.label);
        }

        m_in_tool_action_label_infos = labels;
    }
}

void Editor::tool_bar_append_action(InToolActionID action1, InToolActionID action2, InToolActionID action3,
                                    const std::string &s)
{
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 5);
    if (action1 == InToolActionID::LMB || action1 == InToolActionID::RMB) {
        std::string icon_name = "action-";
        if (action1 == InToolActionID::LMB) {
            icon_name += "lmb";
        }
        else {
            icon_name += "rmb";
        }
        icon_name += "-symbolic";
        auto img = Gtk::manage(new Gtk::Image);
        img->set_from_icon_name(icon_name);
        img->show();
        box->append(*img);
    }
    else {
        auto key_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
        for (const auto action : {action1, action2, action3}) {
            if (action != InToolActionID::NONE) {
                const auto &prefs = m_in_tool_key_sequeces_preferences.keys;
                if (prefs.count(action)) {
                    if (prefs.at(action).size()) {
                        auto seq = prefs.at(action).front();
                        auto kl = Gtk::manage(new Gtk::Label(key_sequence_to_string_short(seq)));
                        kl->set_valign(Gtk::Align::BASELINE);
                        key_box->append(*kl);
                    }
                }
            }
        }
        key_box->get_style_context()->add_class("editor-key-box");

        box->append(*key_box);
    }
    const auto &as = s.size() ? s : in_tool_action_catalog.at(action1).name;

    auto la = Gtk::manage(new Gtk::Label(as));
    la->set_valign(Gtk::Align::BASELINE);

    la->show();

    box->append(*la);

    m_win.tool_bar_append_action(*box);
}

void Editor::update_version_info()
{
    if (!m_core.has_documents()) {
        m_win.set_version_info("");
        return;
    }
    const auto &doc = m_core.get_current_document();
    auto &ver = doc.m_version;
    m_win.set_version_info(ver.get_message());
}

bool Editor::has_file(const std::filesystem::path &path)
{
    return m_core.get_idocument_info_by_path(path);
}

void Editor::open_file(const std::filesystem::path &path)
{
    if (has_file(path))
        return;
    for (auto win : m_win.get_app().get_windows()) {
        if (auto appwin = dynamic_cast<Dune3DAppWindow *>(win)) {
            if (appwin->has_file(path)) {
                appwin->present();
                return;
            }
        }
    }
    add_to_recent_docs(path);
    try {
        CanvasUpdater canvas_updater{*this};

        const UUID doc_uu = UUID::random();

        std::map<UUID, WorkspaceView> loaded_workspace_views;
        try {
            const auto workspace_filename = get_workspace_filename_from_document_filename(path);
            if (std::filesystem::is_regular_file(workspace_filename)) {
                const auto j = load_json_from_file(workspace_filename);
                loaded_workspace_views = WorkspaceView::load_from_json(j.at("workspace_views"));
            }
        }
        catch (...) {
            loaded_workspace_views.clear();
        }

        DocumentView *new_dv = nullptr;
        UUID current_wsv;
        if (loaded_workspace_views.size()) {
            for (const auto &[uu, wv] : loaded_workspace_views) {
                auto &dv = wv.m_documents.at({});
                auto r = m_workspace_views.emplace(uu, wv);
                auto &inserted_wv = r.first->second;
                inserted_wv.m_documents.emplace(doc_uu, dv);
                if (r.second) {
                    inserted_wv.m_current_document = doc_uu;
                    append_workspace_view_page(wv.m_name, uu);
                    set_current_workspace_view(uu);
                    current_wsv = uu;
                }
            }
        }
        else {
            current_wsv = create_workspace_view();
            m_workspace_views.at(current_wsv).m_current_document = doc_uu;
            set_current_workspace_view(current_wsv);
            auto &dv = m_workspace_views.at(current_wsv).m_documents[doc_uu];
            dv.m_document_is_visible = true;
            new_dv = &dv;
        }

        m_core.add_document(path, doc_uu);

        {
            auto &wv = m_workspace_views.at(m_current_workspace_view);
            if (m_core.has_documents() && m_core.get_current_idocument_info().get_uuid() != wv.m_current_document)
                reset_sketch_editing_state();
            m_core.set_current_document(wv.m_current_document);
            m_core.set_current_group(get_current_document_view().m_current_group);
        }

        if (new_dv) {
            new_dv->m_current_group = m_core.get_idocument_info(doc_uu).get_current_group();
        }
        if (current_wsv && m_core.get_current_idocument_info().get_uuid() == doc_uu) {
            auto &dv = m_workspace_views.at(current_wsv).m_documents[doc_uu];
            set_current_group(dv.m_current_group);
        }

        update_workspace_view_names();
        update_can_close_workspace_view_pages();
        m_win.get_app().add_recent_item(path);
        update_title();
        update_version_info();
        update_view_hints();


        load_linked_documents(doc_uu);
    }
    CATCH_LOG(Logger::Level::WARNING, "error opening document" + path_to_string(path), Logger::Domain::DOCUMENT)
}

void Editor::load_linked_documents(const UUID &uu_doc)
{
    if (!m_core.has_documents())
        return;
    auto &doci = m_core.get_idocument_info(uu_doc);
    auto all_documents = m_core.get_documents();
    for (auto &[uu, en] : doci.get_document().m_entities) {
        if (auto en_doc = dynamic_cast<EntityDocument *>(en.get())) {
            // fill in referenced document
            const auto path = en_doc->get_path(doci.get_dirname());

            auto referenced_doc =
                    std::ranges::find_if(all_documents, [&path](const auto &x) { return x->get_path() == path; });
            if (referenced_doc == all_documents.end()) {
                open_file(path);
            }
        }
    }
}

void Editor::set_current_group(const UUID &uu_group)
{
    DUNE3D_TRACE(DebugCategory::UI);
    CanvasUpdater canvas_updater{*this};

    m_core.set_current_group(uu_group);
    auto &group = m_core.get_current_document().get_group(uu_group);
    if (group.get_type() == Group::Type::SKETCH && !m_sketch_editing && group.m_active_wrkpl) {
        auto &workplane = m_core.get_current_document().get_entity<EntityWorkplane>(group.m_active_wrkpl);
        if (workplane.m_visible) {
            workplane.m_visible = false;
            m_core.set_needs_save();
        }
    }
    m_workspace_browser->update_current_group(get_current_document_views());
    update_workplane_label();
    m_constraints_box->update();
    update_group_editor();
    update_action_sensitivity();
    update_action_bar_buttons_sensitivity();
    update_sketch_mode_ui();
    update_selection_editor();
}

void Editor::tool_bar_set_tool_tip(const std::string &s)
{
    m_win.tool_bar_set_tool_tip(s);
}

void Editor::tool_bar_flash(const std::string &s)
{
    m_win.tool_bar_flash(s);
}

void Editor::tool_bar_flash_replace(const std::string &s)
{
    m_win.tool_bar_flash_replace(s);
}

bool Editor::get_use_workplane() const
{
    return m_win.get_workplane_checkbutton().get_active();
}

void Editor::set_constraint_icons(glm::vec3 p, glm::vec3 v, const std::vector<ConstraintType> &constraints)
{
    m_constraint_tip_icons = constraints;
    m_constraint_tip_pos = p;
    m_constraint_tip_vec = v;
}

DocumentView &Editor::get_current_document_view()
{
    return get_current_workspace_view().m_documents[m_core.get_current_idocument_info().get_uuid()];
}

std::map<UUID, DocumentView> &Editor::get_current_document_views()
{
    return get_current_workspace_view().m_documents;
}

WorkspaceView &Editor::get_current_workspace_view()
{
    return m_workspace_views.at(m_current_workspace_view);
}

void Editor::update_title()
{
    if (m_core.has_documents()) {
        auto &doc = m_core.get_current_idocument_info();
        if (doc.has_path())
            m_win.set_window_title_from_path(doc.get_path());
        else
            m_win.set_window_title("New Document");
    }
    else {
        m_win.set_window_title("");
    }
}

Glib::RefPtr<Pango::Context> Editor::get_pango_context()
{
    return m_win.create_pango_context();
}


void Editor::set_buffer(std::unique_ptr<const Buffer> buffer)
{
    m_win.get_app().m_buffer = std::move(buffer);
}

const Buffer *Editor::get_buffer() const
{
    return m_win.get_app().m_buffer.get();
}

void Editor::set_first_update_group(const UUID &group)
{
    m_update_groups_after = group;
}

} // namespace dune3d
