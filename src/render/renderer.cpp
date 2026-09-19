#include "renderer.hpp"
#include "canvas/icanvas.hpp"
#include "document/document.hpp"
#include "document/entity/all_entities.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/group/group_extrude.hpp"
#include "document/group/group_sketch.hpp"
#include "document/group/group_reference.hpp"
#include "document/constraint/all_constraints.hpp"
#include "document/solid_model/solid_model.hpp"
#include "canvas/selectable_ref.hpp"
#include "workspace/idocument_view.hpp"
#include "workspace/iworkspace_view.hpp"
#include "workspace/entity_view.hpp"
#include "util/util.hpp"
#include "util/glm_util.hpp"
#include "icon_texture_id.hpp"
#include "core/idocument_provider.hpp"
#include "core/idocument_info.hpp"
#include "util/fs_util.hpp"
#include "util/arc_util.hpp"
#include "util/paths.hpp"
#include "util/template_util.hpp"
#include "logger/logger.hpp"
#include "canvas/bitmap_font_util.hpp"
#include <array>
#include <iomanip>
#include <limits>
#include <ranges>
#include <sstream>
#include <glm/gtx/io.hpp>
#include <format>
#include <fstream>
#include <GL/glu.h>

namespace {
struct TessVertex {
    double x, y, z;
    size_t index = 0;
};

struct TessOutput {
    std::vector<std::tuple<size_t, size_t, size_t>> triangles;
    GLenum mode = GL_TRIANGLES;
    std::vector<size_t> current;
};

void tess_begin(GLenum mode, void *data)
{
    static_cast<TessOutput *>(data)->mode = mode;
    static_cast<TessOutput *>(data)->current.clear();
}

void tess_vertex(void *vertex_data, void *data)
{
    static_cast<TessOutput *>(data)->current.push_back(static_cast<TessVertex *>(vertex_data)->index);
}

void tess_end(void *data)
{
    auto &out = *static_cast<TessOutput *>(data);
    const auto &v = out.current;
    if (out.mode == GL_TRIANGLES) {
        for (size_t i = 0; i + 2 < v.size(); i += 3)
            out.triangles.emplace_back(v[i], v[i + 1], v[i + 2]);
    }
    else if (out.mode == GL_TRIANGLE_FAN) {
        for (size_t i = 1; i + 1 < v.size(); i++)
            out.triangles.emplace_back(v[0], v[i], v[i + 1]);
    }
    else if (out.mode == GL_TRIANGLE_STRIP) {
        for (size_t i = 2; i < v.size(); i++) {
            if (i % 2)
                out.triangles.emplace_back(v[i - 1], v[i - 2], v[i]);
            else
                out.triangles.emplace_back(v[i - 2], v[i - 1], v[i]);
        }
    }
}

void tess_error(GLenum, void *)
{
}
}

namespace dune3d {

using IconID = IconTexture::IconTextureID;

class Renderer::AutoSaveRestore {
public:
    [[nodiscard]]
    AutoSaveRestore(Renderer &r)
        : m_r(r)
    {
        m_r.save({});
    }

    ~AutoSaveRestore()
    {
        m_r.restore({});
    }

private:
    Renderer &m_r;
};

Renderer::Renderer(ICanvas &ca, IDocumentProvider &docprv) : m_ca(ca), m_doc_prv(docprv)
{
}

void Renderer::draw_sketch_grid(const EntityWorkplane &wrkpl)
{
    if (!m_render_sketch_grid || !m_current_group || m_current_group->m_active_wrkpl != wrkpl.m_uuid)
        return;

    // The default camera distance is 100 and corresponds to a 25-unit major
    // interval. The grid follows the actual camera scale directly.
    constexpr double default_camera_distance = 100.0;
    const double world_per_pixel = std::max<double>(m_ca.get_world_units_per_pixel(), 1e-9);
    // Canvas zoom changes the camera distance by 2^(1/10) per wheel step.
    // Use explicit cumulative wheel-step thresholds because the requested
    // grid intervals include both 2x and 5x changes.
    const double zoom_ratio = std::max(m_ca.get_cam_distance() / default_camera_distance, 1e-12);
    const int zoom_steps = static_cast<int>(std::llround(std::log2(zoom_ratio) * 10.0));
    int grid_index = 10; // 25-unit interval at the default zoom.
    constexpr std::array<int, 8> zoom_out_thresholds = {10, 20, 33, 43, 53, 66, 76, 86};
    constexpr std::array<int, 10> zoom_in_thresholds = {23, 46, 56, 66, 89, 112, 122, 132, 155, 178};
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
    constexpr std::array<double, 19> grid_intervals = {
            0.0001, 0.0005, 0.0025, 0.005, 0.01, 0.05, 0.25, 0.5, 1.0, 5.0,
            25.0,   50.0,   100.0,  250.0, 500.0, 1000.0, 2500.0, 5000.0, 10000.0};
    const double label_spacing = grid_intervals.at(static_cast<size_t>(grid_index));
    const double major_spacing = label_spacing / 5.0;
    const bool show_minor_grid = label_spacing <= 25.0;
    const double minor_spacing = major_spacing / 5.0;

    const auto viewport = m_ca.get_viewport_size();
    const double half_x = viewport.x * world_per_pixel / 2.0;
    const double half_y = viewport.y * world_per_pixel / 2.0;
    // Treat the grid as infinite, but generate only the square that can be
    // visible around the current camera center, with one interval of margin.
    // The diagonal radius also covers the viewport when the view is rolled.
    const auto camera_center = wrkpl.project(glm::dvec3(m_ca.get_cam_center()));
    const double half_extent = std::hypot(half_x, half_y) + label_spacing;
    const double min_x = camera_center.x - half_extent;
    const double max_x = camera_center.x + half_extent;
    const double min_y = camera_center.y - half_extent;
    const double max_y = camera_center.y + half_extent;

    // Keep the grid just behind a supporting solid face.  This preserves the
    // solid's depth occlusion while avoiding coplanar depth flicker.
    const auto grid_offset = m_sketch_grid_offset.value_or(-wrkpl.get_normal_vector() * 1e-4);
    const auto to_world = [&wrkpl, &grid_offset](double x, double y) {
        return wrkpl.transform({x, y}) + grid_offset;
    };
    int label_precision = 0;
    double precision_scale = 1.0;
    while (label_precision < 8) {
        const double scaled_interval = label_spacing * precision_scale;
        if (std::abs(scaled_interval - std::round(scaled_interval)) < 1e-8)
            break;
        label_precision++;
        precision_scale *= 10.0;
    }
    const auto format_grid_value = [label_precision](double value) {
        std::ostringstream stream;
        if (label_precision == 0) {
            stream << static_cast<long long>(std::llround(value));
        }
        else {
            stream << std::fixed << std::setprecision(label_precision) << value;
        }
        return stream.str();
    };
    const auto draw_grid_line = [this, &to_world](double x1, double y1, double x2, double y2) {
        m_ca.draw_line(to_world(x1, y1), to_world(x2, y2));
    };

    const int first_x = static_cast<int>(std::floor(min_x / minor_spacing));
    const int last_x = static_cast<int>(std::ceil(max_x / minor_spacing));
    const int first_y = static_cast<int>(std::floor(min_y / minor_spacing));
    const int last_y = static_cast<int>(std::ceil(max_y / minor_spacing));
    m_ca.save();
    m_ca.set_vertex_inactive(true);
    if (show_minor_grid) {
        m_ca.set_line_style(ICanvas::LineStyle::THINNER);
        for (int i = first_x; i <= last_x; i++) {
            const double position = i * minor_spacing;
            draw_grid_line(position, min_y, position, max_y);
        }
        for (int i = first_y; i <= last_y; i++) {
            const double position = i * minor_spacing;
            draw_grid_line(min_x, position, max_x, position);
        }
    }

    // Re-draw the major lines with the normal line width so they remain
    // visually distinct from the minor grid.
    m_ca.set_line_style(ICanvas::LineStyle::THIN);
    const int first_major_x = static_cast<int>(std::floor(min_x / major_spacing));
    const int last_major_x = static_cast<int>(std::ceil(max_x / major_spacing));
    const int first_major_y = static_cast<int>(std::floor(min_y / major_spacing));
    const int last_major_y = static_cast<int>(std::ceil(max_y / major_spacing));
    for (int i = first_major_x; i <= last_major_x; i++) {
        const double position = i * major_spacing;
        draw_grid_line(position, min_y, position, max_y);
    }
    for (int i = first_major_y; i <= last_major_y; i++) {
        const double position = i * major_spacing;
        draw_grid_line(min_x, position, max_x, position);
    }

    // Draw the sketch axes last so they remain visible over the light grid.
    m_ca.set_vertex_inactive(false);
    // In sketch mode the X axis should terminate at the sketch geometry's
    // left and right bounds.  The rectangle tool updates its line entities
    // while they are being resized, so using the current entity positions
    // keeps the axis in sync with the live rectangle preview as well.
    double axis_min_x = std::numeric_limits<double>::max();
    double axis_max_x = std::numeric_limits<double>::lowest();
    bool have_sketch_geometry = false;
    for (const auto &entry : m_doc->m_entities) {
        const auto &entity = entry.second;
        if (entity->m_group != m_current_group->m_uuid || entity->get_type() != Entity::Type::LINE_2D)
            continue;
        const auto &line = dynamic_cast<const EntityLine2D &>(*entity);
        if (line.m_wrkpl != wrkpl.m_uuid)
            continue;
        axis_min_x = std::min({axis_min_x, line.m_p1.x, line.m_p2.x});
        axis_max_x = std::max({axis_max_x, line.m_p1.x, line.m_p2.x});
        have_sketch_geometry = true;
    }
    if (!have_sketch_geometry)
        axis_min_x = min_x, axis_max_x = max_x;
    m_ca.draw_axis_line(to_world(axis_min_x, 0), to_world(axis_max_x, 0), ICanvas::Axis::X);
    m_ca.draw_axis_line(to_world(0, min_y), to_world(0, max_y), ICanvas::Axis::Y);

    // Put scale labels alongside the major grid lines. The text is drawn in
    // the workplane so it follows the sketch when the plane is not XY.
    m_ca.set_vertex_inactive(false);
    const auto label_normal = glm::quat(wrkpl.m_normal)
                              * glm::angleAxis(static_cast<float>(M_PI) / 2, glm::vec3(0, 0, 1));
    // Keep labels at a constant screen size while the grid interval changes.
    // At the default camera distance, the previous 25-unit grid used 5% of
    // the interval as its label size; scale that world size with zoom.
    constexpr double default_label_size = 25.0 * 0.05;
    const float label_size = static_cast<float>(default_label_size
                                                * m_ca.get_cam_distance() / default_camera_distance);
    const double label_offset = show_minor_grid ? minor_spacing * 0.25 : major_spacing * 0.05;
    const double x_label_offset = show_minor_grid ? minor_spacing * 0.25 : major_spacing * 0.05;
    const int first_label_x = static_cast<int>(std::ceil(min_x / label_spacing));
    const int last_label_x = static_cast<int>(std::floor(max_x / label_spacing));
    const int first_label_y = static_cast<int>(std::ceil(min_y / label_spacing));
    const int last_label_y = static_cast<int>(std::floor(max_y / label_spacing));
    for (int i = first_label_x; i <= last_label_x; i++) {
        if (i == 0)
            continue;
        const double value = i * label_spacing;
        const auto label = format_grid_value(value);
        const float glyph_scale = label_size * .0546f;
        float label_width = 0;
        for (const auto codepoint : label) {
            auto info = bitmap_font::get_glyph_info(codepoint);
            if (!info.is_valid())
                info = bitmap_font::get_glyph_info('?');
            label_width += static_cast<float>(info.advance) * glyph_scale;
        }
        const auto x_label_origin = to_world(value, -x_label_offset)
                                    + glm::dvec3(glm::rotate(label_normal, glm::vec3(-label_width, 0, 0)));
        m_ca.draw_bitmap_text_3d(x_label_origin, label_normal, label_size, label);
    }
    for (int i = first_label_y; i <= last_label_y; i++) {
        if (i == 0)
            continue;
        const double value = i * label_spacing;
        const auto label = format_grid_value(value);
        float label_width = 0;
        const float glyph_scale = label_size * .0546f;
        for (const auto codepoint : label) {
            auto info = bitmap_font::get_glyph_info(codepoint);
            if (!info.is_valid())
                info = bitmap_font::get_glyph_info('?');
            label_width += static_cast<float>(info.advance) * glyph_scale;
        }
        const auto y_label_origin = to_world(label_offset + label_size, value)
                                    + glm::dvec3(glm::rotate(label_normal, glm::vec3(-label_width, 0, 0)));
        m_ca.draw_bitmap_text_3d(y_label_origin, label_normal, label_size, label);
    }
    m_ca.restore();
}

bool Renderer::group_is_visible(const UUID &uu) const
{
    auto &group = m_doc->get_group(uu);
    if (!m_doc_view->group_is_visible(uu))
        return false;
    auto body = group.find_body(*m_doc);
    if (m_current_body_group != &body.group && !m_doc_view->body_is_visible(body.group.m_uuid))
        return false;
    return true;
}

glm::dvec3 Renderer::get_sketch_geometry_offset() const
{
    if (m_is_current_document && m_current_group && m_current_group->get_type() == Group::Type::SKETCH)
        return glm::dvec3(m_ca.get_cam_normal()) * 1e-4;
    return {};
}

void Renderer::render(const Document &doc, const UUID &current_group, const IDocumentView &doc_view,
                      const IWorkspaceView &wrk_view, const std::filesystem::path &containing_dir,
                      std::optional<SelectableRef> sr)
{
    m_doc = &doc;
    m_doc_view = &doc_view;
    m_workspace_view = &wrk_view;
    m_current_group = &doc.get_group(current_group);
    m_current_body_group = &m_current_group->find_body(doc).group;
    m_is_current_document = !sr.has_value();
    m_containing_dir = containing_dir;
    m_curvature_comb_scale = m_workspace_view->get_curvature_comb_scale();

    int first_group_index = 0;
    if (m_first_group)
        first_group_index = doc.get_group(m_first_group).get_index();

    if (sr)
        m_ca.set_override_selectable(*sr);


    if (m_solid_model_edge_select_mode) {
        auto last_solid_model = SolidModel::get_last_solid_model(*m_doc, *m_current_group);
        if (last_solid_model) {
            m_ca.add_face_group(last_solid_model->m_faces, {0, 0, 0}, glm::quat_identity<float, glm::defaultp>(),
                                ICanvas::FaceColor::SOLID_MODEL);
            for (const auto &[edge_idx, path] : last_solid_model->m_edges) {
                for (size_t i = 1; i < path.size(); i++) {
                    m_ca.add_selectable(m_ca.draw_line(path.at(i - 1), path.at(i)),
                                        SelectableRef{SelectableRef::Type::SOLID_MODEL_EDGE, UUID(), edge_idx});
                }
            }
        }

        m_doc = nullptr;
        m_doc_view = nullptr;
        m_workspace_view = nullptr;
        m_current_group = nullptr;
        return;
    }

    for (auto &[uu, group] : doc.get_groups()) {
        const bool is_visible_extrusion_source =
                m_current_group && m_current_group->get_type() == Group::Type::EXTRUDE &&
                group->m_uuid == dynamic_cast<const GroupExtrude &>(*m_current_group).m_source_group &&
                group_is_visible(group->m_uuid);
        if (group->get_index() < first_group_index && !is_visible_extrusion_source)
            continue;
        if (m_is_current_document && m_current_group && m_current_group->get_type() == Group::Type::EXTRUDE) {
            const auto &extrude = dynamic_cast<const GroupExtrude &>(*m_current_group);
            if (group->m_uuid == extrude.m_source_group) {
                std::ofstream log("/tmp/dune3d-visibility-debug.log", std::ios::app);
                log << "renderer source_sketch=" << static_cast<std::string>(group->m_uuid) << " checked="
                    << m_doc_view->group_is_visible(group->m_uuid) << " accepted="
                    << group_is_visible(group->m_uuid) << " current_group="
                    << static_cast<std::string>(m_current_group->m_uuid) << '\n';
            }
        }
        if (!group_is_visible(group->m_uuid))
            continue;
        set_chunk_from_group(*group);
        if (m_is_current_document && m_render_extrusion_editor && group->get_type() == Group::Type::EXTRUDE
            && group->m_uuid == current_group) {
            const auto &extrude = dynamic_cast<const GroupExtrude &>(*group);
            const auto &workplane = doc.get_entity<EntityWorkplane>(extrude.m_wrkpl);
            glm::dvec2 profile_min{std::numeric_limits<double>::max()};
            glm::dvec2 profile_max{std::numeric_limits<double>::lowest()};
            bool have_profile_point = false;
            const auto sketch_paths = paths::Paths::from_document(doc, extrude.m_wrkpl, extrude.m_source_group);
            for (size_t profile_idx = 0; profile_idx < sketch_paths.paths.size(); profile_idx++) {
                if ((!extrude.m_source_paths.empty() && !extrude.m_source_paths.contains(profile_idx))
                    || (extrude.m_source_paths.empty() && extrude.m_source_path
                        && profile_idx != *extrude.m_source_path))
                    continue;
                const auto &path = sketch_paths.paths.at(profile_idx);
                if (path.size() == 1) {
                    if (const auto *circle = dynamic_cast<const EntityCircle2D *>(&path.front().second.entity)) {
                        profile_min = glm::min(profile_min, circle->m_center - glm::dvec2{circle->m_radius});
                        profile_max = glm::max(profile_max, circle->m_center + glm::dvec2{circle->m_radius});
                        have_profile_point = true;
                    }
                }
                else {
                    for (const auto &[node, edge] : path) {
                        profile_min = glm::min(profile_min, node.p);
                        profile_max = glm::max(profile_max, node.p);
                        have_profile_point = true;
                    }
                }
            }
            const auto profile_center = have_profile_point ? (profile_min + profile_max) / 2. : glm::dvec2{0, 0};
            const auto base = workplane.transform(profile_center) + glm::dvec3{m_ca.get_cam_normal()} * 0.25;
            const auto tip = base + extrude.m_dvec;
            const auto handle = SelectableRef{SelectableRef::Type::EXTRUSION_HANDLE, group->m_uuid, 0};
            m_ca.add_selectable(m_ca.draw_line(base, tip), handle);
            m_ca.add_selectable(m_ca.draw_point(tip, IconID::POINT_DIAMOND), handle);
        }
        bool is_extrusion_source_overlay = false;
        if (m_is_current_document && m_render_extrusion_editor && m_current_group
            && m_current_group->get_type() == Group::Type::EXTRUDE) {
            const auto &extrude = dynamic_cast<const GroupExtrude &>(*m_current_group);
            is_extrusion_source_overlay = group->m_uuid == extrude.m_source_group;
        }
        if (m_is_current_document && !m_render_sketch_grid && !is_extrusion_source_overlay
            && group->get_type() == Group::Type::SKETCH && group->m_active_wrkpl) {
            const auto &workplane = doc.get_entity<EntityWorkplane>(group->m_active_wrkpl);
            const auto sketch_paths = paths::Paths::from_document(doc, workplane.m_uuid, group->m_uuid);
            static std::string last_profile_debug_signature;
            const auto profile_debug_signature = std::format("paths={} cells={}", sketch_paths.paths.size(),
                                                              sketch_paths.cells.size());
            if (profile_debug_signature != last_profile_debug_signature) {
                std::ofstream log("/tmp/dune3d-profile-debug.log", std::ios::app);
                log << "renderer " << profile_debug_signature << '\n';
                for (size_t i = 0; i < sketch_paths.cells.size(); i++) {
                    log << "cell " << i << " boundary=" << sketch_paths.cells.at(i).boundary << " holes=";
                    for (const auto hole : sketch_paths.cells.at(i).holes)
                        log << hole << ',';
                    log << '\n';
                }
                last_profile_debug_signature = profile_debug_signature;
            }
            std::vector<size_t> profile_order;
            for (size_t i = 0; i < sketch_paths.paths.size(); i++)
                profile_order.push_back(i);
            const auto profile_area = [&sketch_paths](size_t index) {
                const auto &path = sketch_paths.paths.at(index);
                if (path.size() == 1) {
                    if (const auto *circle = dynamic_cast<const EntityCircle2D *>(&path.front().second.entity))
                        return M_PI * circle->m_radius * circle->m_radius;
                    return std::numeric_limits<double>::max();
                }
                double area = 0;
                for (size_t i = 0; i < path.size(); i++) {
                    const auto &a = path.at(i).first.p;
                    const auto &b = path.at((i + 1) % path.size()).first.p;
                    area += a.x * b.y - b.x * a.y;
                }
                return std::abs(area);
            };
            // Coplanar faces use depth testing for picking, with the later
            // face winning in this renderer. Draw larger regions first so
            // enclosed regions remain on top and selectable.
            std::ranges::sort(profile_order, [](size_t a, size_t b) {
                return a > b;
            });
            std::ranges::stable_sort(profile_order, [&profile_area](size_t a, size_t b) {
                return profile_area(a) > profile_area(b);
            });
            const bool has_selected_profiles = m_selected_sketch_profile_group
                                               && *m_selected_sketch_profile_group == group->m_uuid
                                               && !m_selected_sketch_profiles.empty();
            {
                static std::string last_selection_signature;
                std::string selection_signature = std::format("group={} selected_group={} selected=",
                                                               static_cast<std::string>(group->m_uuid),
                                                               m_selected_sketch_profile_group
                                                                   ? static_cast<std::string>(*m_selected_sketch_profile_group)
                                                                   : "none");
                for (const auto index : m_selected_sketch_profiles)
                    selection_signature += std::format("{},", index);
                selection_signature += std::format(" active={}", has_selected_profiles);
                if (selection_signature != last_selection_signature) {
                    std::ofstream log("/tmp/dune3d-profile-debug.log", std::ios::app);
                    log << "renderer " << selection_signature << '\n';
                    last_selection_signature = selection_signature;
                }
            }
            size_t profile_layer = 0;
            const auto make_profile_vertices = [&workplane, &sketch_paths](size_t index) {
                std::vector<face::Vertex> vertices;
                const auto &path = sketch_paths.paths.at(index);
                if (path.size() == 1) {
                    if (const auto *circle = dynamic_cast<const EntityCircle2D *>(&path.front().second.entity)) {
                        constexpr unsigned int n_segments = 48;
                        for (unsigned int i = 0; i < n_segments; i++) {
                            const auto a = 2 * M_PI * i / n_segments;
                            const auto p = workplane.transform(circle->m_center
                                                               + glm::dvec2{std::cos(a), std::sin(a)} * circle->m_radius);
                            vertices.emplace_back(p.x, p.y, p.z);
                        }
                    }
                }
                else {
                    for (const auto &[node, edge] : path) {
                        if (const auto *arc = dynamic_cast<const EntityArc2D *>(&edge.entity)) {
                            const auto point = node.get_pt_for_edge(edge);
                            const auto other_point = point == 1 ? 2u : 1u;
                            const auto start = edge.get_point(point);
                            const auto end = edge.get_point(other_point);
                            const auto radius = glm::length(arc->m_center - arc->m_from);
                            const auto start_angle = angle(start - edge.transform(arc->m_center));
                            const auto end_angle = angle(end - edge.transform(arc->m_center));
                            const unsigned int segments = 32;
                            double delta = c2pi(end_angle - start_angle);
                            if (point == 2) {
                                delta = end_angle - start_angle;
                                while (delta > 0)
                                    delta -= 2 * M_PI;
                            }
                            if (std::abs(delta) < 1e-2)
                                delta = 2 * M_PI;
                            for (unsigned int i = 0; i < segments; i++) {
                                const auto p = workplane.transform(edge.transform(
                                        arc->m_center + euler(radius, start_angle + delta * i / segments)));
                                vertices.emplace_back(p.x, p.y, p.z);
                            }
                        }
                        else {
                            const auto p = workplane.transform(node.p);
                            vertices.emplace_back(p.x, p.y, p.z);
                        }
                    }
                }
                return vertices;
            };
            for (const auto profile_idx : profile_order) {
                const bool profile_selected = m_selected_sketch_profiles.contains(profile_idx);
                if (has_selected_profiles && !profile_selected)
                    continue;
                face::Face profile;
                profile.color = face::Color{0.2, 0.65, 1.0};
                profile.vertices = make_profile_vertices(profile_idx);
                if (profile.vertices.size() >= 3) {
                    const auto cell = std::ranges::find_if(sketch_paths.cells, [profile_idx](const auto &candidate) {
                        return candidate.boundary == profile_idx && !candidate.holes.empty();
                    });
                    std::vector<TessVertex> tess_vertices;
                    TessOutput tess_output;
                    if (cell != sketch_paths.cells.end()) {
                        auto add_contour = [&](const std::vector<face::Vertex> &contour) {
                            const auto first = tess_vertices.size();
                            for (const auto &vertex : contour) {
                                tess_vertices.push_back({vertex.x, vertex.y, vertex.z, profile.vertices.size()});
                                profile.vertices.push_back(vertex);
                            }
                            return std::pair{first, tess_vertices.size()};
                        };
                        profile.vertices.clear();
                        std::vector<std::pair<size_t, size_t>> contours;
                        contours.push_back(add_contour(make_profile_vertices(cell->boundary)));
                        for (const auto hole : cell->holes)
                            contours.push_back(add_contour(make_profile_vertices(hole)));

                        GLUtesselator *tess = gluNewTess();
                        gluTessCallback(tess, GLU_TESS_BEGIN_DATA, reinterpret_cast<void (*)()>(&tess_begin));
                        gluTessCallback(tess, GLU_TESS_VERTEX_DATA, reinterpret_cast<void (*)()>(&tess_vertex));
                        gluTessCallback(tess, GLU_TESS_END_DATA, reinterpret_cast<void (*)()>(&tess_end));
                        gluTessCallback(tess, GLU_TESS_ERROR_DATA, reinterpret_cast<void (*)()>(&tess_error));
                        gluTessProperty(tess, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ODD);
                        gluTessBeginPolygon(tess, &tess_output);
                        for (const auto [begin, end] : contours) {
                            gluTessBeginContour(tess);
                            for (size_t i = begin; i < end; i++) {
                                auto &vertex = tess_vertices.at(i);
                                gluTessVertex(tess, &vertex.x, &vertex);
                            }
                            gluTessEndContour(tess);
                        }
                        gluTessEndPolygon(tess);
                        gluDeleteTess(tess);
                        profile.triangle_indices = std::move(tess_output.triangles);
                    }
                    auto offset = glm::vec3(get_sketch_geometry_offset());
                    // Keep enclosed profiles in front of their containing
                    // profile in both the visible and pick passes.
                    offset -= glm::vec3(m_ca.get_cam_normal()) * static_cast<float>(profile_layer) * 1e-3f;
                    for (auto &vertex : profile.vertices)
                        vertex += face::Vertex{offset.x, offset.y, offset.z};
                    const auto normal = workplane.get_normal_vector();
                    for (size_t i = 0; i < profile.vertices.size(); i++)
                        profile.normals.emplace_back(normal.x, normal.y, normal.z);
                    if (profile.triangle_indices.empty()) {
                        for (size_t i = 1; i + 1 < profile.vertices.size(); i++)
                            profile.triangle_indices.emplace_back(0, i, i + 1);
                    }
                    const auto vref = m_ca.add_face_group({profile}, {0, 0, 0},
                                                          glm::quat_identity<float, glm::defaultp>(),
                                                          ICanvas::FaceColor::SKETCH_PROFILE);
                    {
                        std::ofstream log("/tmp/dune3d-profile-debug.log", std::ios::app);
                        log << std::format("renderer submit profile={} selected={} layer={} vertices={} triangles={}\n",
                                           profile_idx, profile_selected, profile_layer, profile.vertices.size(),
                                           profile.triangle_indices.size());
                    }
                    m_ca.add_selectable(vref, SelectableRef{SelectableRef::Type::SKETCH_PROFILE,
                                                            group->m_uuid, profile_idx});
                }
                profile_layer++;
            }
        }
        for (const auto &[uu, el] : doc.m_entities) {
            if (el->m_group == group->m_uuid)
                render(*el);
        }
    }


    auto groups_by_body = doc.get_groups_by_body();
    for (auto body_groups : groups_by_body) {
        if (!m_doc_view->body_solid_model_is_visible(body_groups.get_group().m_uuid))
            continue;
        const SolidModel *last_solid_model = nullptr;
        const Group *last_solid_model_group = nullptr;
        for (auto group : body_groups.groups) {
            if (!group_is_visible(group->m_uuid))
                continue;
            if (auto gr = dynamic_cast<const IGroupSolidModel *>(group)) {
                if (gr->get_solid_model()) {
                    last_solid_model = gr->get_solid_model();
                    last_solid_model_group = group;
                }
            }
        }

        if (last_solid_model && last_solid_model_group->get_index() >= first_group_index) {
            const auto is_current = std::ranges::any_of(
                    body_groups.groups, [current_group](auto group) { return group->m_uuid == current_group; });
            auto color = is_current ? ICanvas::FaceColor::SOLID_MODEL : ICanvas::FaceColor::OTHER_BODY_SOLID_MODEL;
            if (body_groups.body.m_color.has_value())
                color = ICanvas::FaceColor::AS_IS;
            set_chunk_from_group(*last_solid_model_group);
            if (m_is_current_document) {
                unsigned int face_idx = 0;
                for (const auto &face : last_solid_model->m_faces) {
                    const auto vref = m_ca.add_face_group({face}, {0, 0, 0},
                                                          glm::quat_identity<float, glm::defaultp>(), color);
                    m_ca.add_selectable(vref, SelectableRef{SelectableRef::Type::SOLID_MODEL_FACE,
                                                            last_solid_model_group->m_uuid, face_idx++});
                }
            }
            else {
                const auto vref = m_ca.add_face_group(last_solid_model->m_faces, {0, 0, 0},
                                                      glm::quat_identity<float, glm::defaultp>(), color);
                if (sr)
                    m_ca.add_selectable(vref, *sr);
            }
        }
    }


    if (!sr && !m_workspace_view->show_only_solid_models()) {
        set_chunk_from_group(*m_current_group);
        for (const auto &[uu, el] : doc.m_constraints) {
            if (!doc.get_groups().contains(el->m_group) || !group_is_visible(el->m_group))
                continue;
            try {
                el->accept(*this);
            }
            catch (const std::exception &ex) {
                Logger::log_critical("exception rendering constraint " + static_cast<std::string>(uu),
                                     Logger::Domain::RENDERER, ex.what());
            }
        }
        draw_constraints();
    }

    m_ca.update_bbox();

    m_doc = nullptr;
    m_doc_view = nullptr;
    m_workspace_view = nullptr;
    m_current_group = nullptr;
    if (sr)
        m_ca.unset_override_selectable();
}

void Renderer::render(const Entity &entity)
{
    // Generated extrusion geometry is already represented by the solid
    // model.  Do not draw its source arcs/circles (or their center markers)
    // over the finished solid.  The extrusion editor intentionally keeps
    // these entities available for its preview.
    if (!m_render_extrusion_editor && entity.m_kind == ItemKind::GENRERATED &&
        m_doc->get_group(entity.m_group).get_type() == Group::Type::EXTRUDE)
        return;
    if (m_render_extrusion_editor
        && entity.of_type(Entity::Type::LINE_2D, Entity::Type::ARC_2D, Entity::Type::CIRCLE_2D,
                          Entity::Type::POINT_2D, Entity::Type::BEZIER_2D, Entity::Type::LINE_3D,
                          Entity::Type::ARC_3D, Entity::Type::CIRCLE_3D, Entity::Type::BEZIER_3D))
        return;
    if (!entity.m_visible) {
        if (entity.get_type() != Entity::Type::WORKPLANE)
            return;
        const auto &workplane = dynamic_cast<const EntityWorkplane &>(entity);
        const auto &reference = m_doc->get_reference_group();
        const bool selector_plane = m_render_sketch_plane_selector && workplane.m_group == reference.m_uuid;
        const bool active_grid_plane = m_sketch_plane_grid && *m_sketch_plane_grid == workplane.m_uuid;
        const bool default_grid_plane = workplane.m_uuid == reference.get_workplane_xy_uuid();
        if ((!reference.m_show_origin || workplane.m_uuid != reference.get_workplane_xy_uuid()) && !selector_plane
            && !active_grid_plane && !default_grid_plane)
            return;
    }
    if (m_workspace_view->show_only_solid_models() && entity.get_type() != Entity::Type::DOCUMENT
        && entity.get_type() != Entity::Type::STEP)
        return;
    if (entity.m_construction
        && ((entity.m_group != m_current_group->m_uuid
             && !m_workspace_view->construction_entities_from_previous_groups_are_visible())
            || !m_is_current_document))
        return;

    AutoSaveRestore asr{*this};
    const bool visible_sketch_group = entity.m_group != m_current_group->m_uuid
                                      && m_doc->get_group(entity.m_group).get_type() == Group::Type::SKETCH
                                      && group_is_visible(entity.m_group);
    m_ca.set_vertex_inactive(entity.m_group != m_current_group->m_uuid && !visible_sketch_group);
    m_ca.set_selection_invisible(entity.m_selection_invisible);
    m_ca.set_vertex_construction(entity.m_construction);
    m_ca.set_show_default_points(m_show_dimension_points);
    try {
        entity.accept(*this);
    }
    catch (const std::exception &ex) {
        Logger::log_critical("exception rendering entity " + static_cast<std::string>(entity.m_uuid),
                             Logger::Domain::RENDERER, ex.what());
    }
}

void Renderer::visit(const EntityLine3D &line)
{
    // The extrusion leader is an editing aid, not part of the model.
    if (line.m_name == "leader")
        return;
    if (line.m_kind == ItemKind::GENRERATED && m_doc->get_group(line.m_group).get_type() == Group::Type::EXTRUDE) {
        const auto *sketch = m_current_group && m_current_group->get_type() == Group::Type::SKETCH
                                     ? dynamic_cast<const GroupSketch *>(m_current_group)
                                     : nullptr;
        const bool show_face_reference = m_render_sketch_grid && sketch && sketch->m_attached_to_face;
        if (!show_face_reference)
            return;
    }
    const bool extrusion_view_only = m_current_group && !m_render_extrusion_editor
                                      && m_current_group->get_type() == Group::Type::EXTRUDE
                                      && line.m_group == m_current_group->m_uuid;
    if (extrusion_view_only) {
        // Generated extrusion side/outline lines are construction geometry;
        // the finished solid supplies the visible result.  Keep them out of
        // the committed view so only the dedicated center handle is shown
        // while extrusion editing is active.
        return;
    }
    m_ca.add_selectable(m_ca.draw_line(line.m_p1, line.m_p2),
                        SelectableRef{SelectableRef::Type::ENTITY, line.m_uuid, 0});
    if (line.m_no_points)
        return;

    m_ca.add_selectable(m_ca.draw_point(line.m_p1), SelectableRef{SelectableRef::Type::ENTITY, line.m_uuid, 1});
    m_ca.add_selectable(m_ca.draw_point(line.m_p2), SelectableRef{SelectableRef::Type::ENTITY, line.m_uuid, 2});
}

void Renderer::visit(const EntityLine2D &line)
{
    auto &wrkpl = dynamic_cast<const EntityWorkplane &>(*m_doc->m_entities.at(line.m_wrkpl));
    const auto offset = get_sketch_geometry_offset();
    const auto p1 = wrkpl.transform(line.m_p1) + offset;
    const auto p2 = wrkpl.transform(line.m_p2) + offset;
    m_ca.add_selectable(m_ca.draw_line(p1, p2), SelectableRef{SelectableRef::Type::ENTITY, line.m_uuid, 0});
    m_ca.add_selectable(m_ca.draw_point(p1), SelectableRef{SelectableRef::Type::ENTITY, line.m_uuid, 1});
    m_ca.add_selectable(m_ca.draw_point(p2), SelectableRef{SelectableRef::Type::ENTITY, line.m_uuid, 2});
}

void Renderer::visit(const EntityPoint2D &point)
{
    auto &wrkpl = dynamic_cast<const EntityWorkplane &>(*m_doc->m_entities.at(point.m_wrkpl));
    const auto p = wrkpl.transform(point.m_p) + get_sketch_geometry_offset();
    m_ca.add_selectable(m_ca.draw_point(p), SelectableRef{SelectableRef::Type::ENTITY, point.m_uuid, 0});
}

namespace {
class ArcDiscretizer {
public:
    ArcDiscretizer(const EntityArc2D &arc)
    {
        m_center = arc.m_center;
        m_radius = glm::length(m_center - arc.m_from);
        const auto a0 = c2pi(angle(arc.m_from - m_center));
        const auto a1 = c2pi(angle(arc.m_to - m_center));
        m_segments = 64;

        m_dphi = c2pi(a1 - a0);
        if (m_dphi < 1e-2)
            m_dphi = 2 * M_PI;
        m_dphi /= m_segments;
        m_a0 = a0;
    }

    bool next(glm::dvec2 &a, glm::dvec2 &b)
    {
        auto phi = m_a0 + m_dphi * m_i;
        a = m_center + euler(m_radius, phi);
        b = m_center + euler(m_radius, phi + m_dphi);
        m_i++;
        return m_i <= m_segments + (m_include_last ? 1 : 0);
    }

    void set_include_last(bool last)
    {
        m_include_last = last;
    }

private:
    glm::dvec2 m_center;
    double m_radius;
    double m_dphi;
    double m_a0;
    unsigned int m_segments;

    unsigned int m_i = 0;
    bool m_include_last = false;
};
} // namespace

void Renderer::visit(const EntityArc2D &arc)
{
    auto &wrkpl = dynamic_cast<const EntityWorkplane &>(*m_doc->m_entities.at(arc.m_wrkpl));
    const auto offset = get_sketch_geometry_offset();

    {
        ArcDiscretizer ad{arc};

        glm ::dvec2 p0, p1;
        while (ad.next(p0, p1)) {
            m_ca.add_selectable(m_ca.draw_line(wrkpl.transform(p0) + offset, wrkpl.transform(p1) + offset),
                                SelectableRef{SelectableRef::Type::ENTITY, arc.m_uuid, 0});
        }
    }
    if (arc.m_group == m_current_group->m_uuid && m_curvature_comb_scale > 0 && !m_state.no_curvature_combs) {
        AutoSaveRestore asr{*this};
        m_ca.set_selection_invisible(true);
        m_ca.set_line_style(ICanvas::LineStyle::THIN);

        const auto curvature = 1 / arc.get_radius() * m_curvature_comb_scale;
        ArcDiscretizer ad{arc};
        ad.set_include_last(true);
        glm::dvec2 p0, p1;
        glm::dvec2 last_comb_pt = {NAN, NAN};
        while (ad.next(p0, p1)) {
            const auto comb_line = glm::normalize(glm::dvec2(p0 - arc.m_center)) * curvature;
            const auto comb_pt = p0 + comb_line;
            m_ca.draw_line(wrkpl.transform(p0), wrkpl.transform(comb_pt));
            if (m_connect_curvature_comb) {
                if (!std::isnan(last_comb_pt.x))
                    m_ca.draw_line(wrkpl.transform(last_comb_pt), wrkpl.transform(comb_pt));
                last_comb_pt = comb_pt;
            }
        }
    }

    m_ca.add_selectable(m_ca.draw_point(wrkpl.transform(arc.m_from) + offset),
                        SelectableRef{SelectableRef::Type::ENTITY, arc.m_uuid, 1});
    m_ca.add_selectable(m_ca.draw_point(wrkpl.transform(arc.m_to) + offset),
                        SelectableRef{SelectableRef::Type::ENTITY, arc.m_uuid, 2});
    m_ca.add_selectable(m_ca.draw_point(wrkpl.transform(arc.m_center) + offset, IconID::POINT_CROSS),
                        SelectableRef{SelectableRef::Type::ENTITY, arc.m_uuid, 3});
}

void Renderer::visit(const EntityCircle2D &circle)
{
    auto &wrkpl = dynamic_cast<const EntityWorkplane &>(*m_doc->m_entities.at(circle.m_wrkpl));
    const auto offset = get_sketch_geometry_offset();

    {
        unsigned int segments = 64;

        float dphi = 2 * M_PI;
        dphi /= segments;
        float a = 0;
        while (segments--) {
            const auto p0 = circle.m_center + euler(circle.m_radius, a);
            const auto p1 = circle.m_center + euler(circle.m_radius, a + dphi);
            m_ca.add_selectable(m_ca.draw_line(wrkpl.transform(p0) + offset, wrkpl.transform(p1) + offset),
                                SelectableRef{SelectableRef::Type::ENTITY, circle.m_uuid, 0});
            a += dphi;
        }
    }

    m_ca.add_selectable(m_ca.draw_point(wrkpl.transform(circle.m_center) + offset, IconID::POINT_CROSS),
                        SelectableRef{SelectableRef::Type::ENTITY, circle.m_uuid, 1});
}
void Renderer::visit(const EntityCircle3D &circle)
{
    {
        unsigned int segments = 64;

        float dphi = 2 * M_PI;
        dphi /= segments;
        float a = 0;
        while (segments--) {
            const auto p0 = circle.m_center + glm::rotate(circle.m_normal, glm::dvec3(euler(circle.m_radius, a), 0));
            const auto p1 =
                    circle.m_center + glm::rotate(circle.m_normal, glm::dvec3(euler(circle.m_radius, a + dphi), 0));
            m_ca.add_selectable(m_ca.draw_line(p0, p1), SelectableRef{SelectableRef::Type::ENTITY, circle.m_uuid, 0});
            a += dphi;
        }
    }

    m_ca.add_selectable(m_ca.draw_point(circle.m_center, IconID::POINT_CROSS),
                        SelectableRef{SelectableRef::Type::ENTITY, circle.m_uuid, 1});
}

void Renderer::visit(const EntityArc3D &arc)
{
    auto un = glm::rotate(arc.m_normal, glm::dvec3(1, 0, 0));
    auto vn = glm::rotate(arc.m_normal, glm::dvec3(0, 1, 0));

    auto project = [&un, &vn, &arc](const glm::dvec3 &p) -> glm::dvec2 {
        auto v = p - arc.m_center;
        return {glm::dot(un, v), glm::dot(vn, v)};
    };
    auto transform = [&arc](const glm::dvec2 &p) { return arc.m_center + glm::rotate(arc.m_normal, glm::dvec3(p, 0)); };

    auto from2 = project(arc.m_from);
    auto to2 = project(arc.m_to);


    {
        const auto radius0 = glm::length(from2);
        const auto a0 = c2pi(angle(from2));
        const auto a1 = c2pi(angle(to2));
        unsigned int segments = 64;

        float dphi = c2pi(a1 - a0);
        if (dphi < 1e-2)
            dphi = 2 * M_PI;
        dphi /= segments;
        float a = a0;
        while (segments--) {
            const auto p0 = euler(radius0, a);
            const auto p1 = euler(radius0, a + dphi);
            m_ca.add_selectable(m_ca.draw_line(transform(p0), transform(p1)),
                                SelectableRef{SelectableRef::Type::ENTITY, arc.m_uuid, 0});
            a += dphi;
        }
    }

    m_ca.add_selectable(m_ca.draw_point(arc.m_from), SelectableRef{SelectableRef::Type::ENTITY, arc.m_uuid, 1});
    m_ca.add_selectable(m_ca.draw_point(arc.m_to), SelectableRef{SelectableRef::Type::ENTITY, arc.m_uuid, 2});
    m_ca.add_selectable(m_ca.draw_point(arc.m_center, IconID::POINT_CROSS),
                        SelectableRef{SelectableRef::Type::ENTITY, arc.m_uuid, 3});
}

void Renderer::visit(const EntityWorkplane &wrkpl)
{
    if (!m_is_current_document)
        return;

    if (m_workspace_view->hide_irrelevant_workplanes() && !m_render_sketch_plane_selector && !m_sketch_plane_grid
        && wrkpl.m_uuid != m_doc->get_reference_group().get_workplane_xy_uuid()) {
        if (wrkpl.m_group != m_current_group->m_uuid && wrkpl.m_uuid != m_current_group->m_active_wrkpl)
            return;
    }

    const auto &reference = m_doc->get_reference_group();
    const bool is_reference_plane = wrkpl.m_group == reference.m_uuid;
    const auto is_reference_plane_uuid = [&reference](const UUID &uuid) {
        return uuid == reference.get_workplane_xy_uuid() || uuid == reference.get_workplane_yz_uuid()
               || uuid == reference.get_workplane_zx_uuid();
    };
    if (m_render_sketch_plane_selector && is_reference_plane) {
        constexpr double gap = 1.5;
        constexpr double size = 5.5;
        // Keep the selector tiles in their positive local quadrants:
        // XY toward +X/+Y, YZ toward +Y/+Z, and XZ toward +Z/+X.
        const double tile_x0 = gap;
        const double tile_x1 = gap + size;
        const double tile_y0 = gap;
        const double tile_y1 = gap + size;
        const std::array<glm::vec2, 4> tile = {
                glm::vec2(tile_x0, tile_y0),
                glm::vec2(tile_x1, tile_y0),
                glm::vec2(tile_x1, tile_y1),
                glm::vec2(tile_x0, tile_y1),
        };
        // Each tile is drawn on its corresponding reference workplane, so it
        // must select that same workplane.
        const auto sr = SelectableRef{SelectableRef::Type::ENTITY, wrkpl.m_uuid, 0};
        const bool hover_is_other_reference_plane = m_sketch_plane_hovered
                                                    && is_reference_plane_uuid(*m_sketch_plane_hovered)
                                                    && *m_sketch_plane_hovered != wrkpl.m_uuid;
        const bool highlight_default_xy = wrkpl.m_uuid == reference.get_workplane_xy_uuid()
                                          && !hover_is_other_reference_plane;
        face::Face selector_face;
        const auto normal = glm::normalize(wrkpl.get_normal_vector());
        for (const auto &point : tile) {
            const auto p = wrkpl.transform(point);
            selector_face.vertices.emplace_back(p.x, p.y, p.z);
            selector_face.normals.emplace_back(normal.x, normal.y, normal.z);
        }
        selector_face.triangle_indices = {{0, 1, 2}, {0, 2, 3}};
        m_ca.add_selectable(m_ca.add_face_group(
                                    {selector_face}, {0, 0, 0}, glm::quat(1, 0, 0, 0),
                                    highlight_default_xy ? ICanvas::FaceColor::SKETCH_PLANE_HIGHLIGHT
                                                         : ICanvas::FaceColor::SKETCH_PLANE),
                            sr);
        for (size_t i = 0; i < tile.size(); i++) {
            const auto p1 = wrkpl.transform(tile.at(i));
            const auto p2 = wrkpl.transform(tile.at((i + 1) % tile.size()));
            m_ca.add_selectable(m_ca.draw_axis_line(
                                        p1, p2,
                                        highlight_default_xy ? ICanvas::Axis::PLANE_HIGHLIGHT
                                                             : ICanvas::Axis::PLANE),
                                sr);
        }
    }
    const bool show_origin = !is_reference_plane || reference.m_show_origin;
    if (show_origin) {
        m_ca.add_selectable(m_ca.draw_point(wrkpl.m_origin, IconID::POINT_DIAMOND),
                            SelectableRef{SelectableRef::Type::ENTITY, wrkpl.m_uuid, 1});
        if (is_reference_plane && wrkpl.m_uuid == reference.get_workplane_xy_uuid()) {
            constexpr float axis_length = 10.0f;
            m_ca.draw_axis_line(wrkpl.m_origin, wrkpl.m_origin + glm::dvec3(axis_length, 0, 0), ICanvas::Axis::X);
            m_ca.draw_axis_line(wrkpl.m_origin, wrkpl.m_origin + glm::dvec3(0, axis_length, 0), ICanvas::Axis::Y);
            m_ca.draw_axis_line(wrkpl.m_origin, wrkpl.m_origin + glm::dvec3(0, 0, axis_length), ICanvas::Axis::Z);
        }
    }
    if (m_render_sketch_plane_selector && is_reference_plane)
        return;
    draw_sketch_grid(wrkpl);
    if (!wrkpl.m_visible) {
        return;
    }
    glm::vec2 sz = wrkpl.m_size / 2.;
    std::array<glm::vec2, 4> pts = {
            glm::vec2(-sz),
            glm::vec2(sz * glm::vec2(1, -1)),
            glm::vec2(sz),
            glm::vec2(sz * glm::vec2(-1, 1)),
    };
    const auto sr = SelectableRef{SelectableRef::Type::ENTITY, wrkpl.m_uuid, 0};

    for (size_t i = 0; i < pts.size(); i++) {
        const auto p1 = wrkpl.transform(pts.at(i));
        const auto p2 = wrkpl.transform(pts.at((i + 1) % (pts.size())));
        m_ca.add_selectable(m_ca.draw_line(p1, p2), sr);
    }

    if (wrkpl.m_uuid == m_current_group->m_active_wrkpl) {
        for (size_t i = 0; i < pts.size(); i++) {
            const auto p1 = wrkpl.transform(pts.at(i) * .99f);
            const auto p2 = wrkpl.transform(pts.at((i + 1) % (pts.size())) * .99f);
            m_ca.add_selectable(m_ca.draw_line(p1, p2), sr);
        }
    }

    auto normal = wrkpl.get_normal_vector() * .05;
    m_ca.add_selectable(m_ca.draw_screen_line(wrkpl.m_origin, normal), sr);

    // draw bottom left chamfer
    {
        auto s = std::min(sz.x, sz.y) / 5;
        auto p1 = wrkpl.transform(-sz + glm::vec2(s, 0));
        auto p2 = wrkpl.transform(-sz + glm::vec2(0, s));
        m_ca.add_selectable(m_ca.draw_line(p1, p2), sr);

        auto label_pos = -sz + glm::vec2(s, s * .25);
        auto label_normal = wrkpl.m_normal;
        if (wrkpl.m_name == "YZ") {
            label_pos = -sz + glm::vec2(s * .25, s);
            label_normal = wrkpl.m_normal
                           * glm::angleAxis(-static_cast<double>(M_PI) / 2, glm::dvec3(0, 0, 1));
        }
        add_selectables(sr, m_ca.draw_bitmap_text_3d(wrkpl.transform(label_pos), label_normal, s / 2,
                                                     wrkpl.m_name));
    }
}

void Renderer::visit(const EntitySTEP &en)
{
    const bool show_only_solids = m_workspace_view->show_only_solid_models();

    if (!show_only_solids)
        m_ca.add_selectable(m_ca.draw_point(en.m_origin, IconID::POINT_DIAMOND),
                            SelectableRef{SelectableRef::Type::ENTITY, en.m_uuid, 1});

    if (!en.m_show_points && !show_only_solids) {
        for (const auto &[idx, p] : en.m_anchors) {
            m_ca.add_selectable(m_ca.draw_point(en.transform(p), IconID::POINT_TRIANGLE_DOWN),
                                SelectableRef{SelectableRef::Type::ENTITY, en.m_uuid, idx});
        }
    }

    auto view = dynamic_cast<const EntityViewSTEP *>(m_doc_view->get_entity_view(en.m_uuid));
    auto display = EntityViewSTEP::Display::SOLID;
    if (view)
        display = view->m_display;

    SelectableRef sr{SelectableRef::Type::ENTITY, en.m_uuid, 0};
    if (en.m_imported) {
        if (any_of(display, EntityViewSTEP::Display::SOLID, EntityViewSTEP::Display::SOLID_WIREFRAME)
            && !en.m_include_in_solid_model)
            m_ca.add_selectable(m_ca.add_face_group(en.m_imported->result.faces, en.m_origin, en.m_normal,
                                                    ICanvas::FaceColor::AS_IS),
                                sr);

        if (any_of(display, EntityViewSTEP::Display::WIREFRAME, EntityViewSTEP::Display::SOLID_WIREFRAME)) {
            for (const auto &path : en.m_imported->result.edges) {
                for (size_t i = 1; i < path.size(); i++) {
                    const auto &a = path.at(i - 1);
                    const auto &b = path.at(i);
                    m_ca.add_selectable(m_ca.draw_line(en.transform({a.x, a.y, a.z}), en.transform({b.x, b.y, b.z})),
                                        sr);
                }
            }
        }
        if (en.m_show_points && !show_only_solids) {
            unsigned int idx = EntitySTEP::s_imported_point_offset;
            for (auto &pt : en.m_imported->result.points) {
                m_ca.add_selectable(m_ca.draw_point(en.transform({pt.x, pt.y, pt.z}), IconID::POINT_TRIANGLE_DOWN),
                                    SelectableRef{SelectableRef::Type::ENTITY, en.m_uuid, idx++});
            }
        }
    }
}

class FakeDocumentView : public IDocumentView {
public:
    bool document_is_visible() const override
    {
        return true;
    }
    bool body_is_visible(const UUID &uu) const override
    {
        return true;
    }
    bool body_solid_model_is_visible(const UUID &uu) const override
    {
        return true;
    }
    bool group_is_visible(const UUID &uu) const override
    {
        return true;
    }
    const EntityView *get_entity_view(const UUID &uu) const override
    {
        return nullptr;
    }
};


void Renderer::visit(const EntityDocument &en)
{
    auto path = en.get_path(m_containing_dir);
    SelectableRef sr_origin{SelectableRef::Type::ENTITY, en.m_uuid, 1};
    m_ca.add_selectable(m_ca.draw_point(en.m_origin, IconID::POINT_DIAMOND), sr_origin);
    auto m = glm::translate(glm::mat4(1), glm::vec3(en.m_origin)) * glm::toMat4(glm::quat(en.m_normal));
    AutoSaveRestore asr{*this};
    m_ca.set_transform(m);

    auto doc = m_doc_prv.get_idocument_info_by_path(path);
    if (doc) {
        Renderer renderer{m_ca, m_doc_prv};
        SelectableRef sr{SelectableRef::Type::ENTITY, en.m_uuid, 0};
        renderer.render(doc->get_document(), doc->get_document().get_groups_sorted().back()->m_uuid, FakeDocumentView{},
                        *m_workspace_view, doc->get_dirname(), sr);
    }
    else {
        add_selectables(sr_origin, m_ca.draw_bitmap_text({0, 0, 0}, 1, path_to_string(en.m_path) + " not loaded"));
    }
}

void Renderer::visit(const EntityBezier2D &bezier)
{
    auto &wrkpl = dynamic_cast<const EntityWorkplane &>(*m_doc->m_entities.at(bezier.m_wrkpl));
    const auto p1 = wrkpl.transform(bezier.m_p1);
    const auto p2 = wrkpl.transform(bezier.m_p2);
    const auto c1 = wrkpl.transform(bezier.m_c1);
    const auto c2 = wrkpl.transform(bezier.m_c2);
    const auto sr = SelectableRef{SelectableRef::Type::ENTITY, bezier.m_uuid, 0};
    unsigned int steps = 64;
    glm::vec2 last = bezier.m_p1;
    for (unsigned int i = 1; i <= steps; i++) {
        const auto t = (double)i / steps;
        const auto p = bezier.get_interpolated(t);
        m_ca.add_selectable(m_ca.draw_line(wrkpl.transform(last), wrkpl.transform(p)), sr);
        last = p;
    }
    if (bezier.m_group == m_current_group->m_uuid && m_curvature_comb_scale > 0 && !m_state.no_curvature_combs) {
        AutoSaveRestore asr{*this};
        m_ca.set_selection_invisible(true);
        m_ca.set_line_style(ICanvas::LineStyle::THIN);

        glm::dvec2 last_comb_pt = {NAN, NAN};
        for (unsigned int i = 0; i <= steps; i++) {
            const auto t = (double)i / steps;
            const auto p = bezier.get_interpolated(t);
            const auto tangent = bezier.get_tangent(t);
            const auto curvature = bezier.get_curvature(t) * m_curvature_comb_scale;
            const auto comb_line = glm::normalize(glm::dvec2(tangent.y, -tangent.x)) * curvature;
            const auto comb_pt = p + comb_line;
            m_ca.draw_line(wrkpl.transform(p), wrkpl.transform(comb_pt));
            if (m_connect_curvature_comb) {
                if (!std::isnan(last_comb_pt.x))
                    m_ca.draw_line(wrkpl.transform(last_comb_pt), wrkpl.transform(comb_pt));
                last_comb_pt = comb_pt;
            }
        }
    }
    if (m_state.no_bezier_control_lines == false) {
        AutoSaveRestore asr{*this};
        m_ca.set_selection_invisible(true);
        m_ca.set_line_style(ICanvas::LineStyle::THIN);
        m_ca.draw_line(p1, c1);
        m_ca.draw_line(p2, c2);
    }

    m_ca.add_selectable(m_ca.draw_point(p1), SelectableRef{SelectableRef::Type::ENTITY, bezier.m_uuid, 1});
    m_ca.add_selectable(m_ca.draw_point(p2), SelectableRef{SelectableRef::Type::ENTITY, bezier.m_uuid, 2});
    m_ca.add_selectable(m_ca.draw_point(c1, IconID::POINT_CIRCLE),
                        SelectableRef{SelectableRef::Type::ENTITY, bezier.m_uuid, 3});
    m_ca.add_selectable(m_ca.draw_point(c2, IconID::POINT_CIRCLE),
                        SelectableRef{SelectableRef::Type::ENTITY, bezier.m_uuid, 4});
}

void Renderer::visit(const EntityBezier3D &bezier)
{
    const auto sr = SelectableRef{SelectableRef::Type::ENTITY, bezier.m_uuid, 0};
    unsigned int steps = 64;
    glm::vec3 last = bezier.m_p1;
    for (unsigned int i = 1; i <= steps; i++) {
        const auto t = (double)i / steps;
        const auto p = bezier.get_interpolated(t);
        m_ca.add_selectable(m_ca.draw_line(last, p), sr);
        last = p;
    }

    m_ca.add_selectable(m_ca.draw_point(bezier.m_p1), SelectableRef{SelectableRef::Type::ENTITY, bezier.m_uuid, 1});
    m_ca.add_selectable(m_ca.draw_point(bezier.m_p2), SelectableRef{SelectableRef::Type::ENTITY, bezier.m_uuid, 2});
}


void Renderer::visit(const EntityCluster &cluster)
{
    auto &wrkpl = dynamic_cast<const EntityWorkplane &>(*m_doc->m_entities.at(cluster.m_wrkpl));
    const auto p = wrkpl.transform(cluster.m_origin);
    m_ca.add_selectable(m_ca.draw_point(p, IconID::POINT_DIAMOND),
                        SelectableRef{SelectableRef::Type::ENTITY, cluster.m_uuid, 1});
    const SelectableRef sr{SelectableRef::Type::ENTITY, cluster.m_uuid, 0};

    if (cluster.m_exploded_group) {
        add_selectables(sr, m_ca.draw_bitmap_text(p, 1,
                                                  "exploded cluster in group "
                                                          + m_doc->get_group(cluster.m_exploded_group).m_name));
        return;
    }


    auto wrkpl_mat = glm::translate(glm::mat4(1), glm::vec3(wrkpl.m_origin)) * glm::toMat4(glm::quat(wrkpl.m_normal));


    auto m = glm::scale(glm::rotate(glm::translate(glm::mat4(1), glm::vec3(cluster.m_origin, 0.)),
                                    (float)glm::radians(cluster.m_angle), glm::vec3(0., 0., 1.)),
                        glm::vec3(cluster.m_scale_x, cluster.m_scale_y, 0.f));
    {
        AutoSaveRestore asr{*this};
        m_state.no_bezier_control_lines = !cluster.m_anchors_available.size();
        m_state.no_curvature_combs = true;
        m_ca.set_transform(wrkpl_mat * m);

        m_ca.set_override_selectable(sr);

        m_ca.set_no_points(true);
        for (const auto &[uu, en] : cluster.m_content->m_entities) {
            if (en->m_construction && !cluster.m_anchors_available.size())
                continue;
            {
                AutoSaveRestore asr2{*this};
                if (cluster.m_anchors_available.size() && en->m_construction)
                    m_ca.set_vertex_construction(true);
                en->accept(*this);
            }
        }

        m_ca.unset_override_selectable();
    }

    if (cluster.m_anchors_available.size()) {
        for (const auto &[i, enp] : cluster.m_anchors_available) {
            m_ca.add_selectable(m_ca.draw_point(wrkpl.transform(cluster.transform(cluster.get_anchor_point(enp))),
                                                IconID::POINT_TRIANGLE_DOWN),
                                SelectableRef{SelectableRef::Type::ENTITY, cluster.m_uuid, i});
        }
    }
    else {
        for (const auto &[i, enp] : cluster.m_anchors) {
            m_ca.add_selectable(m_ca.draw_point(wrkpl.transform(cluster.transform(cluster.get_anchor_point(enp))),
                                                IconID::POINT_TRIANGLE_DOWN),
                                SelectableRef{SelectableRef::Type::ENTITY, cluster.m_uuid, i});
        }
    }
}

void Renderer::visit(const EntityText &text)
{
    auto &wrkpl = dynamic_cast<const EntityWorkplane &>(*m_doc->m_entities.at(text.m_wrkpl));
    const auto p = wrkpl.transform(text.m_origin);
    m_ca.add_selectable(m_ca.draw_point(p, IconID::POINT_DIAMOND),
                        SelectableRef{SelectableRef::Type::ENTITY, text.m_uuid, 1});
    const SelectableRef sr{SelectableRef::Type::ENTITY, text.m_uuid, 0};

    auto wrkpl_mat = glm::translate(glm::mat4(1), glm::vec3(wrkpl.m_origin)) * glm::toMat4(glm::quat(wrkpl.m_normal));


    auto m = glm::scale(glm::rotate(glm::translate(glm::mat4(1), glm::vec3(text.m_origin, 0.)),
                                    (float)glm::radians(text.m_angle), glm::vec3(0., 0., 1.)),
                        glm::vec3(text.m_scale, text.m_scale, 0.f));
    {
        AutoSaveRestore asr{*this};
        m_state.no_bezier_control_lines = true;
        m_state.no_curvature_combs = true;
        m_ca.set_transform(wrkpl_mat * m);

        m_ca.set_override_selectable(sr);

        m_ca.set_no_points(true);
        for (const auto &[uu, en] : text.m_content->m_entities) {
            en->accept(*this);
        }

        m_ca.unset_override_selectable();
    }
    for (const auto &[i, p] : text.m_anchors) {
        m_ca.add_selectable(m_ca.draw_point(wrkpl.transform(text.transform(p)), IconID::POINT_TRIANGLE_DOWN),
                            SelectableRef{SelectableRef::Type::ENTITY, text.m_uuid, i});
    }
}

void Renderer::visit(const EntityPicture &pic)
{
    auto &wrkpl = dynamic_cast<const EntityWorkplane &>(*m_doc->m_entities.at(pic.m_wrkpl));
    const auto p = wrkpl.transform(pic.m_origin);
    m_ca.add_selectable(m_ca.draw_point(p, IconID::POINT_DIAMOND),
                        SelectableRef{SelectableRef::Type::ENTITY, pic.m_uuid, 1});
    if (pic.m_data) {
        std::array<glm::vec3, 4> corners;
        for (size_t i = 0; i < corners.size(); i++) {
            corners.at(i) = pic.get_point(10 + i, *m_doc);
        }
        m_ca.add_selectable(m_ca.draw_picture(corners, pic.m_data),
                            SelectableRef{SelectableRef::Type::ENTITY, pic.m_uuid, 0});
    }
    else {
        add_selectables(SelectableRef{SelectableRef::Type::ENTITY, pic.m_uuid, 0},
                        m_ca.draw_bitmap_text(p, 1, "picture data not loaded: " + (std::string)pic.m_data_uuid));
    }

    for (const auto &[i, p] : pic.m_anchors) {
        m_ca.add_selectable(m_ca.draw_point(wrkpl.transform(pic.transform(p)), IconID::POINT_TRIANGLE_DOWN),
                            SelectableRef{SelectableRef::Type::ENTITY, pic.m_uuid, i});
    }
}

static glm::vec3 project_point_onto_plane(const glm::vec3 &plane_origin, const glm::vec3 &plane_normal,
                                          const glm::vec3 &point)
{
    const auto v = point - plane_origin;
    const auto dist = glm::dot(plane_normal, v);
    return point - dist * plane_normal;
}

static std::string format_datum(const Document &doc, const IConstraintDatum &dat)
{
    const auto s = dat.format_datum(dat.get_display_datum(doc));
    if (dat.is_measurement())
        return "(" + s + ")";
    else
        return s;
}

void Renderer::visit(const ConstraintPointDistance &constr)
{
    AutoSaveRestore asr{*this};
    m_ca.set_vertex_constraint(true);
    glm::vec3 from = m_doc->get_point(constr.m_entity1);
    glm::vec3 to = m_doc->get_point(constr.m_entity2);
    auto p = constr.get_origin(*m_doc) + constr.m_offset;
    glm::vec3 fallback_normal = {NAN, NAN, NAN};
    if (constr.m_wrkpl) {
        auto &wrkpl = m_doc->get_entity<EntityWorkplane>(constr.m_wrkpl);
        p = wrkpl.project3(p);
        fallback_normal = wrkpl.get_normal_vector();
        from = wrkpl.project3(from);
        to = wrkpl.project3(to);
    }

    const auto label = format_datum(*m_doc, constr);
    draw_distance_line(from, to, p, label, constr.m_uuid, fallback_normal);
}

void Renderer::visit(const ConstraintPointDistanceAligned &constr)
{
    AutoSaveRestore asr{*this};
    m_ca.set_vertex_constraint(true);
    glm::vec3 from = m_doc->get_point(constr.m_entity1);
    glm::vec3 to = m_doc->get_point(constr.m_entity2);
    auto p = constr.get_origin(*m_doc) + constr.m_offset;
    glm::vec3 fallback_normal = {NAN, NAN, NAN};
    if (constr.m_wrkpl) {
        auto &wrkpl = m_doc->get_entity<EntityWorkplane>(constr.m_wrkpl);
        p = wrkpl.project3(p);
        fallback_normal = wrkpl.get_normal_vector();
        from = wrkpl.project3(from);
        to = wrkpl.project3(to);
    }

    const auto label = format_datum(*m_doc, constr);
    draw_distance_line_with_direction(from, to, constr.get_align_vector(*m_doc), p, label, constr.m_uuid,
                                      fallback_normal);
}


static const float constraint_arrow_scale = .015;
static const float constraint_arrow_aspect = 1.5;
static const float constraint_line_extension = 2.0;


void Renderer::draw_distance_line(const glm::vec3 &from, const glm::vec3 &to, const glm::vec3 &text_p,
                                  const std::string &label, const UUID &uu, const glm::vec3 &fallback_normal)
{
    draw_distance_line_with_direction(from, to, from - to, text_p, label, uu, fallback_normal);
}

void Renderer::draw_distance_line_with_direction(const glm::vec3 &from, const glm::vec3 &to, const glm::vec3 &dir,
                                                 const glm::vec3 &text_p, const std::string &label, const UUID &uu,
                                                 const glm::vec3 &fallback_normal)
{
    m_ca.set_line_style(ICanvas::LineStyle::THINNER);
    auto n = glm::normalize(dir);
    if (glm::dot(glm::normalize(from - to), n) < 0)
        n *= -1;
    auto p1 = project_point_onto_plane(from, n, text_p);
    auto p2 = project_point_onto_plane(to, n, text_p);

    SelectableRef sr{SelectableRef::Type::CONSTRAINT, uu, 0};
    m_ca.add_selectable(m_ca.draw_line(p1, p2), sr);
    m_ca.add_selectable(m_ca.draw_line(from, p1), sr);
    m_ca.add_selectable(m_ca.draw_line(to, p2), sr);

    // The offset controls where the dimension line is placed, but the label
    // should remain centered on the rendered measurement segment.  Using the
    // raw offset point here made newly-created labels appear far along the
    // line whenever the second selection/cursor was not at its midpoint.
    const auto screen_from = m_ca.project_to_window(p1);
    const auto screen_to = m_ca.project_to_window(p2);
    auto label_angle = static_cast<float>(std::atan2(-(screen_to.y - screen_from.y), screen_to.x - screen_from.x));
    if (label_angle > glm::half_pi<float>())
        label_angle -= glm::pi<float>();
    else if (label_angle <= -glm::half_pi<float>())
        label_angle += glm::pi<float>();
    add_selectables(sr, m_ca.draw_bitmap_text_centered((p1 + p2) / 2.f, 0.75f, label, label_angle));

    const float scale = constraint_arrow_scale;
    const float aspect = constraint_arrow_aspect;
    const float ext = constraint_line_extension;
    auto d1 = p1 - from;
    if (glm::length(d1) > 1e-6) {
        d1 = glm::normalize(d1);
    }
    else {
        if (std::isnan(fallback_normal.x))
            return;
        d1 = glm::normalize(glm::cross(n, fallback_normal));
    }
    auto d2 = p2 - to;
    if (glm::length(d2) > 1e-6) {
        d2 = glm::normalize(d2);
    }
    else {
        if (std::isnan(fallback_normal.x))
            return;
        d2 = glm::normalize(glm::cross(n, fallback_normal));
    }

    m_ca.add_selectable(m_ca.draw_screen_line(p1, d1 * ext * scale), sr);
    m_ca.add_selectable(m_ca.draw_screen_line(p2, d2 * ext * scale), sr);
    m_ca.add_selectable(m_ca.draw_screen_line(p1, (+d1 - n * aspect) * scale), sr);
    m_ca.add_selectable(m_ca.draw_screen_line(p1, (-d1 - n * aspect) * scale), sr);
    m_ca.add_selectable(m_ca.draw_screen_line(p2, (+d2 + n * aspect) * scale), sr);
    m_ca.add_selectable(m_ca.draw_screen_line(p2, (-d2 + n * aspect) * scale), sr);
}

void Renderer::add_selectables(const SelectableRef &sr, const std::vector<ICanvas::VertexRef> &vrs)
{
    for (const auto &vr : vrs) {
        m_ca.add_selectable(vr, sr);
    }
}

void Renderer::visit(const ConstraintPointLineDistance &constr)
{
    AutoSaveRestore asr{*this};
    m_ca.set_vertex_constraint(true);

    auto pp = m_doc->get_point(constr.m_point);
    auto pproj = constr.get_projected(*m_doc);
    auto p = constr.get_origin(*m_doc) + constr.m_offset;
    glm::vec3 fallback_normal = {NAN, NAN, NAN};
    if (constr.m_wrkpl) {
        auto &wrkpl = m_doc->get_entity<EntityWorkplane>(constr.m_wrkpl);
        p = wrkpl.project3(p);
        fallback_normal = wrkpl.get_normal_vector();
        pproj = wrkpl.project3(pproj);
        pp = wrkpl.project3(pp);
    }

    draw_distance_line(pproj, pp, p, format_datum(*m_doc, constr), constr.m_uuid, fallback_normal);
}

void Renderer::visit(const ConstraintPointPlaneDistance &constr)
{
    AutoSaveRestore asr{*this};

    m_ca.set_vertex_constraint(true);

    const auto pp = m_doc->get_point(constr.m_point);
    const auto pproj = constr.get_projected(*m_doc);
    auto p = constr.get_origin(*m_doc) + constr.m_offset;

    const auto &l1 = m_doc->get_entity(constr.m_line1);
    const auto fallback_normal = l1.get_point(2, *m_doc) - l1.get_point(1, *m_doc);

    draw_distance_line(pproj, pp, p, format_datum(*m_doc, constr), constr.m_uuid, fallback_normal);
}

void Renderer::visit(const ConstraintDiameterRadius &constr)
{
    AutoSaveRestore asr{*this};

    m_ca.set_vertex_constraint(true);
    m_ca.set_line_style(ICanvas::LineStyle::THINNER);
    auto &en = m_doc->get_entity(constr.m_entity);
    auto &en_radius = dynamic_cast<const IEntityRadius &>(en);
    auto &en_wrkpl = dynamic_cast<const IEntityInWorkplane &>(en);
    auto &wrkpl = m_doc->get_entity<EntityWorkplane>(en_wrkpl.get_workplane());
    const auto center = en_radius.get_center();
    const auto radius = en_radius.get_radius();
    const auto offset_norm = glm::normalize(constr.m_offset);


    glm::vec3 from = wrkpl.transform(center);
    if (constr.get_type() == Constraint::Type::DIAMETER)
        from = wrkpl.transform(center + offset_norm * -radius);
    glm::vec3 to = wrkpl.transform(center + offset_norm * radius);
    auto p = wrkpl.transform(center + constr.m_offset);


    auto l = glm::normalize(from - to);
    glm::vec3 n = wrkpl.transform_relative(glm::vec2(-offset_norm.y, offset_norm.x));

    SelectableRef sr{SelectableRef::Type::CONSTRAINT, constr.m_uuid, 0};
    m_ca.add_selectable(m_ca.draw_line(from, to), sr);
    const auto scale = constraint_arrow_scale;
    const auto aspect = constraint_arrow_aspect;
    if (constr.get_type() == Constraint::Type::DIAMETER) {
        m_ca.add_selectable(m_ca.draw_screen_line(from, (+n - l * aspect) * scale), sr);
        m_ca.add_selectable(m_ca.draw_screen_line(from, (-n - l * aspect) * scale), sr);
    }
    m_ca.add_selectable(m_ca.draw_screen_line(to, (+n + l * aspect) * scale), sr);
    m_ca.add_selectable(m_ca.draw_screen_line(to, (-n + l * aspect) * scale), sr);

    const auto label = format_datum(*m_doc, constr);
    const auto screen_from = m_ca.project_to_window(from);
    const auto screen_to = m_ca.project_to_window(to);
    auto label_angle = static_cast<float>(std::atan2(-(screen_to.y - screen_from.y), screen_to.x - screen_from.x));
    if (label_angle > glm::half_pi<float>())
        label_angle -= glm::pi<float>();
    else if (label_angle <= -glm::half_pi<float>())
        label_angle += glm::pi<float>();
    add_selectables(sr, m_ca.draw_bitmap_text_centered(p, 0.75f, label, label_angle));
}

void Renderer::visit(const ConstraintPointDistanceHV &constr)
{
    AutoSaveRestore asr{*this};

    m_ca.set_vertex_constraint(true);
    m_ca.set_line_style(ICanvas::LineStyle::THINNER);
    auto &wrkpl = m_doc->get_entity<EntityWorkplane>(constr.m_wrkpl);
    auto from = wrkpl.project(m_doc->get_point(constr.m_entity1));
    auto to = wrkpl.project(m_doc->get_point(constr.m_entity2));
    glm::vec2 mid = (from + to) / 2.;
    auto p = wrkpl.project(wrkpl.transform(mid) + constr.m_offset);

    // Keep vertical rectangle dimensions on the left edge by default, and
    // horizontal dimensions on the bottom edge by default. Move their visual
    // extension lines to the opposite edge once the annotation is dragged
    // past the rectangle's midpoint. The constraints themselves remain
    // attached to their original geometry.
    if (constr.get_type() == Constraint::Type::POINT_DISTANCE_VERTICAL ||
        constr.get_type() == Constraint::Type::POINT_DISTANCE_HORIZONTAL) {
        if (const auto *dimension_line = dynamic_cast<const EntityLine2D *>(
                    &m_doc->get_entity(constr.m_entity1.entity))) {
            constexpr double tolerance = 1e-6;
            if (constr.get_type() == Constraint::Type::POINT_DISTANCE_VERTICAL) {
                const auto y1 = std::min(dimension_line->m_p1.y, dimension_line->m_p2.y);
                const auto y2 = std::max(dimension_line->m_p1.y, dimension_line->m_p2.y);
                double left_x = std::numeric_limits<double>::infinity();
                double right_x = -std::numeric_limits<double>::infinity();

                for (const auto &[uuid, entity] : m_doc->m_entities) {
                    const auto *line = dynamic_cast<const EntityLine2D *>(entity.get());
                    if (!line || line->m_wrkpl != dimension_line->m_wrkpl ||
                        std::abs(line->m_p2.x - line->m_p1.x) > tolerance)
                        continue;
                    const auto line_y1 = std::min(line->m_p1.y, line->m_p2.y);
                    const auto line_y2 = std::max(line->m_p1.y, line->m_p2.y);
                    if (std::abs(line_y1 - y1) > tolerance || std::abs(line_y2 - y2) > tolerance)
                        continue;
                    left_x = std::min(left_x, line->m_p1.x);
                    right_x = std::max(right_x, line->m_p1.x);
                }

                if (std::isfinite(left_x) && std::isfinite(right_x) && right_x > left_x + tolerance) {
                    const auto anchor_x = p.x > (left_x + right_x) / 2. ? right_x : left_x;
                    from = {anchor_x, from.y};
                    to = {anchor_x, to.y};
                }
            }
            else {
                const auto x1 = std::min(dimension_line->m_p1.x, dimension_line->m_p2.x);
                const auto x2 = std::max(dimension_line->m_p1.x, dimension_line->m_p2.x);
                double bottom_y = std::numeric_limits<double>::infinity();
                double top_y = -std::numeric_limits<double>::infinity();

                for (const auto &[uuid, entity] : m_doc->m_entities) {
                    const auto *line = dynamic_cast<const EntityLine2D *>(entity.get());
                    if (!line || line->m_wrkpl != dimension_line->m_wrkpl ||
                        std::abs(line->m_p2.y - line->m_p1.y) > tolerance)
                        continue;
                    const auto line_x1 = std::min(line->m_p1.x, line->m_p2.x);
                    const auto line_x2 = std::max(line->m_p1.x, line->m_p2.x);
                    if (std::abs(line_x1 - x1) > tolerance || std::abs(line_x2 - x2) > tolerance)
                        continue;
                    bottom_y = std::min(bottom_y, line->m_p1.y);
                    top_y = std::max(top_y, line->m_p1.y);
                }

                if (std::isfinite(bottom_y) && std::isfinite(top_y) && top_y > bottom_y + tolerance) {
                    const auto anchor_y = p.y > (bottom_y + top_y) / 2. ? top_y : bottom_y;
                    from = {from.x, anchor_y};
                    to = {to.x, anchor_y};
                }
            }
        }
    }
    const double scale = constraint_arrow_scale;
    const double aspect = constraint_arrow_aspect;
    const double ext = constraint_line_extension;
    SelectableRef sr{SelectableRef::Type::CONSTRAINT, constr.m_uuid, 0};

    glm::dvec2 pf, pt;
    if (constr.get_type() == Constraint::Type::POINT_DISTANCE_HORIZONTAL) {
        pf = {from.x, p.y};
        pt = {to.x, p.y};
    }
    else {
        pf = {p.x, from.y};
        pt = {p.x, to.y};
    }

    const auto v = glm::normalize(pf - pt);
    const auto d = wrkpl.transform_relative(v);
    const auto dnf = wrkpl.transform_relative(glm::normalize(pf - from));
    const auto dnt = wrkpl.transform_relative(glm::normalize(pt - to));
    const auto pft = wrkpl.transform(pf);
    const auto ptt = wrkpl.transform(pt);
    m_ca.add_selectable(m_ca.draw_line(wrkpl.transform(from), pft), sr);
    m_ca.add_selectable(m_ca.draw_screen_line(pft, dnf * scale * ext), sr);
    m_ca.add_selectable(m_ca.draw_screen_line(pft, (+dnf - d * aspect) * scale), sr);
    m_ca.add_selectable(m_ca.draw_screen_line(pft, (-dnf - d * aspect) * scale), sr);
    m_ca.add_selectable(m_ca.draw_line(pft, ptt), sr);


    m_ca.add_selectable(m_ca.draw_screen_line(ptt, dnt * scale * ext), sr);
    m_ca.add_selectable(m_ca.draw_screen_line(ptt, (+dnt + d * aspect) * scale), sr);
    m_ca.add_selectable(m_ca.draw_screen_line(ptt, (-dnt + d * aspect) * scale), sr);
    m_ca.add_selectable(m_ca.draw_line(ptt, wrkpl.transform(to)), sr);

    const auto label = format_datum(*m_doc, constr);
    const auto screen_from = m_ca.project_to_window(pft);
    const auto screen_to = m_ca.project_to_window(ptt);
    auto angle = static_cast<float>(std::atan2(-(screen_to.y - screen_from.y), screen_to.x - screen_from.x));
    if (angle > glm::half_pi<float>())
        angle -= glm::pi<float>();
    else if (angle <= -glm::half_pi<float>())
        angle += glm::pi<float>();
    add_selectables(sr, m_ca.draw_bitmap_text_centered(wrkpl.transform(p), 0.75f, label, angle));
}

void Renderer::visit(const ConstraintPointsCoincident &constraint)
{
    auto p1 = m_doc->get_point(constraint.m_entity1);
    auto p2 = m_doc->get_point(constraint.m_entity2);
    add_constraint(p1, IconID::CONSTRAINT_POINTS_COINCIDENT, constraint.m_uuid);
    if (glm::length(p2 - p1) > 1e-6)
        add_constraint(p2, IconID::CONSTRAINT_POINTS_COINCIDENT, constraint.m_uuid);
}

void Renderer::visit(const ConstraintHV &constraint)
{
    auto p1 = m_doc->get_point(constraint.m_entity1);
    auto p2 = m_doc->get_point(constraint.m_entity2);
    auto icon = IconID::CONSTRAINT_HORIZONTAL;
    add_constraint((p1 + p2) / 2., icon, constraint.m_uuid, p2 - p1);
}

void Renderer::visit(const ConstraintSymmetricHV &constraint)
{
    auto p1 = m_doc->get_point(constraint.m_entity1);
    auto p2 = m_doc->get_point(constraint.m_entity2);
    auto icon = IconID::CONSTRAINT_SYMMETRIC_VERTICAL;
    add_constraint((p1 + p2) / 2., icon, constraint.m_uuid, p2 - p1);
}

void Renderer::visit(const ConstraintSymmetricLine &constraint)
{
    auto p1 = m_doc->get_point(constraint.m_entity1);
    auto p2 = m_doc->get_point(constraint.m_entity2);
    auto icon = IconID::CONSTRAINT_SYMMETRIC_LINE;
    add_constraint((p1 + p2) / 2., icon, constraint.m_uuid, p2 - p1);
}

static glm::dvec3 get_vec(const UUID &uu, const Document &doc)
{
    return doc.get_point({uu, 2}) - doc.get_point({uu, 1});
}

void Renderer::visit(const ConstraintPointOnLine &constraint)
{
    const auto pt = m_doc->get_point(constraint.m_point);
    const auto v = get_vec(constraint.m_line, *m_doc);
    add_constraint(pt, IconID::CONSTRAINT_POINT_ON_LINE, constraint.m_uuid, v);
}

void Renderer::visit(const ConstraintPointOnCircle &constraint)
{
    const auto pt = m_doc->get_point(constraint.m_point);
    const auto &en = m_doc->get_entity(constraint.m_circle);
    glm::dvec3 center;
    if (en.of_type(Entity::Type::CIRCLE_2D, Entity::Type::CIRCLE_3D))
        center = en.get_point(1, *m_doc);
    else
        center = en.get_point(3, *m_doc);
    const auto v = pt - center;

    glm::dquat normal;
    if (auto iw = dynamic_cast<const IEntityInWorkplane *>(&en))
        normal = m_doc->get_entity<EntityWorkplane>(iw->get_workplane()).get_normal();
    else if (auto arc = dynamic_cast<const EntityArc3D *>(&en))
        normal = arc->m_normal;
    else if (auto circle = dynamic_cast<const EntityCircle3D *>(&en))
        normal = circle->m_normal;
    auto normal_vec = glm::rotate(normal, glm::dvec3(0, 0, 1));
    auto ortho = glm::cross(v, normal_vec);

    add_constraint(pt, IconID::CONSTRAINT_POINT_ON_CIRCLE, constraint.m_uuid, ortho);
}

void Renderer::visit(const ConstraintWorkplaneNormal &constraint)
{
    auto pt = m_doc->get_entity<EntityWorkplane>(constraint.m_wrkpl).m_origin;
    add_constraint(pt, IconID::CONSTRAINT_WORKPLANE_NORMAL, constraint.m_uuid);
}

void Renderer::visit(const ConstraintMidpoint &constraint)
{
    auto pt = m_doc->get_point(constraint.m_point);
    const auto v = get_vec(constraint.m_line, *m_doc);
    add_constraint(pt, IconID::CONSTRAINT_MIDPOINT, constraint.m_uuid, v);
}

static glm::vec3 get_center(const Entity &entity, const Document &doc)
{
    if (auto wrkpl = dynamic_cast<const EntityWorkplane *>(&entity)) {
        return wrkpl->m_origin;
    }
    else if (entity.get_type() == Entity::Type::CIRCLE_2D) {
        return entity.get_point(1, doc);
    }
    else {
        return (entity.get_point(1, doc) + entity.get_point(2, doc)) / 2.;
    }
}

void Renderer::visit(const ConstraintParallel &constraint)
{
    auto c1 = get_center(m_doc->get_entity(constraint.m_entity1), *m_doc);
    auto c2 = get_center(m_doc->get_entity(constraint.m_entity2), *m_doc);
    const auto v1 = get_vec(constraint.m_entity1, *m_doc);
    const auto v2 = get_vec(constraint.m_entity2, *m_doc);
    add_constraint(c1, IconID::CONSTRAINT_PARALLEL, constraint.m_uuid, v1);
    add_constraint(c2, IconID::CONSTRAINT_PARALLEL, constraint.m_uuid, v2);
}

void Renderer::visit(const ConstraintEqualLength &constraint)
{
    auto c1 = get_center(m_doc->get_entity(constraint.m_entity1), *m_doc);
    auto c2 = get_center(m_doc->get_entity(constraint.m_entity2), *m_doc);
    const auto v1 = get_vec(constraint.m_entity1, *m_doc);
    const auto v2 = get_vec(constraint.m_entity2, *m_doc);
    add_constraint(c1, IconID::CONSTRAINT_EQUAL_LENGTH, constraint.m_uuid, v1);
    add_constraint(c2, IconID::CONSTRAINT_EQUAL_LENGTH, constraint.m_uuid, v2);
}

static glm::dvec3 length_indicator_vec(const Entity &entity, const Document &doc)
{
    if (entity.of_type(Entity::Type::LINE_2D, Entity::Type::LINE_3D))
        return get_vec(entity.m_uuid, doc);

    if (entity.of_type(Entity::Type::ARC_2D, Entity::Type::ARC_3D))
        return entity.get_point(2, doc) - entity.get_point(1, doc);

    return glm::dvec3(0, 0, 0);
}

void Renderer::visit(const ConstraintLengthRatio &constraint)
{
    AutoSaveRestore asr{*this};
    m_ca.set_vertex_constraint(true);

    auto &e1 = m_doc->get_entity(constraint.m_entity1);
    auto &e2 = m_doc->get_entity(constraint.m_entity2);

    const auto c1 = get_center(e1, *m_doc);
    const auto c2 = get_center(e2, *m_doc);

    add_constraint(c1, IconID::CONSTRAINT_LENGTH_RATIO, constraint.m_uuid, length_indicator_vec(e1, *m_doc));
    add_constraint(c2, IconID::CONSTRAINT_LENGTH_RATIO, constraint.m_uuid, length_indicator_vec(e2, *m_doc));

    auto position = constraint.get_origin(*m_doc) + constraint.get_offset();
    if (constraint.m_wrkpl) {
        auto &wrkpl = m_doc->get_entity<EntityWorkplane>(constraint.m_wrkpl);
        position = wrkpl.project3(position);
    }

    const auto label = format_datum(*m_doc, constraint);
    add_selectables(SelectableRef{SelectableRef::Type::CONSTRAINT, constraint.m_uuid, 0},
                    m_ca.draw_bitmap_text_centered(position, 1, label));
}

void Renderer::visit(const ConstraintEqualRadius &constraint)
{
    auto c1 = get_center(m_doc->get_entity(constraint.m_entity1), *m_doc);
    auto c2 = get_center(m_doc->get_entity(constraint.m_entity2), *m_doc);
    add_constraint(c1, IconID::CONSTRAINT_EQUAL_LENGTH, constraint.m_uuid);
    add_constraint(c2, IconID::CONSTRAINT_EQUAL_LENGTH, constraint.m_uuid);
}

void Renderer::visit(const ConstraintSameOrientation &constraint)
{
    auto pt1 = m_doc->get_point({constraint.m_entity1, 1});
    auto pt2 = m_doc->get_point({constraint.m_entity2, 1});
    add_constraint(pt1, IconID::CONSTRAINT_SAME_ORIENTATION, constraint.m_uuid);
    add_constraint(pt2, IconID::CONSTRAINT_SAME_ORIENTATION, constraint.m_uuid);
}

void Renderer::visit(const ConstraintLockRotation &constraint)
{
    auto pt = m_doc->get_point({constraint.m_entity, 1});
    add_constraint(pt, IconID::CONSTRAINT_LOCK_ROTATION, constraint.m_uuid);
}

void Renderer::visit(const ConstraintArcArcTangent &constraint, IconID icon)
{
    auto p1 = m_doc->get_point(constraint.m_arc1);
    const auto &arc = m_doc->get_entity(constraint.m_arc1.entity);
    const auto &wrkpl =
            m_doc->get_entity<EntityWorkplane>(dynamic_cast<const IEntityInWorkplane &>(arc).get_workplane());
    const auto v = wrkpl.transform_relative(
            dynamic_cast<const IEntityTangent &>(arc).get_tangent_at_point(constraint.m_arc1.point));
    add_constraint(p1, icon, constraint.m_uuid, v);
}

void Renderer::visit(const ConstraintArcArcTangent &constraint)
{
    visit(constraint, IconID::CONSTRAINT_ARC_ARC_TANGENT);
}

void Renderer::visit(const ConstraintBezierBezierTangentSymmetric &constraint)
{
    visit(constraint, IconID::CONSTRAINT_BEZIER_BEZIER_TANGENT_SYMMETRIC);
}

void Renderer::visit(const ConstraintLinePointsPerpendicular &constraint)
{
    auto p1 = m_doc->get_point(constraint.m_point_line);
    add_constraint(p1, IconID::CONSTRAINT_PERPENDICULAR, constraint.m_uuid);
}

void Renderer::visit(const ConstraintLinesPerpendicular &constraint)
{
    auto c1 = get_center(m_doc->get_entity(constraint.m_entity1), *m_doc);
    auto c2 = get_center(m_doc->get_entity(constraint.m_entity2), *m_doc);
    const auto v1 = get_vec(constraint.m_entity1, *m_doc);
    const auto v2 = get_vec(constraint.m_entity2, *m_doc);
    add_constraint(c1, IconID::CONSTRAINT_PERPENDICULAR, constraint.m_uuid, v1);
    add_constraint(c2, IconID::CONSTRAINT_PERPENDICULAR, constraint.m_uuid, v2);
}


void Renderer::visit(const ConstraintLinesAngle &constr)
{
    AutoSaveRestore asr{*this};

    m_ca.set_vertex_constraint(true);

    auto is = constr.get_origin(*m_doc);

    const auto vecs = constr.get_vectors(*m_doc);

    auto p = is + constr.m_offset;
    if (constr.m_wrkpl) {
        auto &wrkpl = m_doc->get_entity<EntityWorkplane>(constr.m_wrkpl);
        p = wrkpl.project3(p);
    }
    else {
        p = project_point_onto_plane(is, vecs.n, p);
    }

    auto vp = p - is;
    auto r = glm::length(vp);
    auto vpu = glm::dot(vecs.u, vp);

    auto transform = [&](const glm::dvec2 &p) { return is + p.x * vecs.u + p.y * vecs.v; };

    SelectableRef sr{SelectableRef::Type::CONSTRAINT, constr.m_uuid, 0};

    {
        float a0 = 0;
        if (vpu < 0)
            a0 = M_PI;
        unsigned int segments = 64;

        auto l1vp = glm::dvec2(glm::dot(vecs.u, vecs.l1v), glm::dot(vecs.v, vecs.l1v));
        auto l2vp = glm::dvec2(glm::dot(vecs.u, vecs.l2v), glm::dot(vecs.v, vecs.l2v));

        float dphi = angle(l2vp) - angle(l1vp);
        if (dphi > M_PI)
            dphi = 2 * M_PI - dphi;

        dphi /= segments;
        float a = a0;
        while (segments--) {
            const auto p0 = euler(r, a);
            const auto p1 = euler(r, a + dphi);
            m_ca.add_selectable(m_ca.draw_line(transform(p0), transform(p1)), sr);
            a += dphi;
        }
    }

    const auto label = format_datum(*m_doc, constr);
    add_selectables(sr, m_ca.draw_bitmap_text_centered(p, 1, label));
}

void Renderer::visit(const ConstraintArcLineTangent &constraint)
{
    const auto p1 = m_doc->get_point(constraint.m_arc);
    const auto &arc = m_doc->get_entity<EntityArc2D>(constraint.m_arc.entity);
    const auto &wrkpl = m_doc->get_entity<EntityWorkplane>(arc.m_wrkpl);
    const auto v = wrkpl.transform_relative(arc.get_tangent_at_point(constraint.m_arc.point));
    add_constraint(p1, IconID::CONSTRAINT_ARC_LINE_TANGENT, constraint.m_uuid, v);
}

void Renderer::visit(const ConstraintPointInPlane &constraint)
{
    auto pt = m_doc->get_point(constraint.m_point);
    add_constraint(pt, IconID::CONSTRAINT_POINT_IN_PLANE, constraint.m_uuid);
}

void Renderer::visit(const ConstraintPointInWorkplane &constraint)
{
    auto pt = m_doc->get_point(constraint.m_point);
    add_constraint(pt, IconID::CONSTRAINT_POINT_IN_PLANE, constraint.m_uuid);
}

void Renderer::visit(const ConstraintBezierLineTangent &constraint)
{
    const auto p1 = m_doc->get_point(constraint.m_bezier);
    const auto &bez = m_doc->get_entity<EntityBezier2D>(constraint.m_bezier.entity);
    const auto &wrkpl = m_doc->get_entity<EntityWorkplane>(bez.m_wrkpl);
    const auto v = wrkpl.transform_relative(bez.get_tangent_at_point(constraint.m_bezier.point));
    add_constraint(p1, IconID::CONSTRAINT_ARC_LINE_TANGENT, constraint.m_uuid, v);
}

void Renderer::visit(const ConstraintPointOnBezier &constraint, IconTexture::IconTextureID icon)
{
    const auto pt = m_doc->get_point(constraint.m_point);
    const auto &bez = m_doc->get_entity<IEntityTangentProjected>(constraint.m_line);
    const auto &wrkpl = m_doc->get_entity<EntityWorkplane>(constraint.m_wrkpl);
    const auto v = wrkpl.transform_relative(bez.get_tangent_in_workplane(constraint.m_val, wrkpl));
    add_constraint(pt, icon, constraint.m_uuid, v);
}

void Renderer::visit(const ConstraintPointOnBezier &constraint)
{
    visit(constraint, IconID::CONSTRAINT_POINT_ON_BEZIER);
}

void Renderer::visit(const ConstraintLineTangentOnBezier &constraint)
{
    visit(constraint, IconID::CONSTRAINT_LINE_TANGENT_ON_BEZIER);
}

void Renderer::visit(const ConstraintLinePerpendicularOnBezier &constraint)
{
    visit(constraint, IconID::CONSTRAINT_LINE_PERPENDICULAR_ON_BEZIER);
}

void Renderer::visit(const ConstraintBezierBezierSameCurvature &constraint)
{
    visit(constraint, IconID::CONSTRAINT_G2);
}

void Renderer::visit(const ConstraintBezierArcSameCurvature &constraint)
{
    visit(constraint, IconID::CONSTRAINT_G2);
}

void Renderer::add_constraint_icons(glm::vec3 p, glm::vec3 v, const std::vector<ConstraintType> &constraints)
{
    using CT = Constraint::Type;
    static const std::map<ConstraintType, IconID> constraint_icon_map = {
            {CT::HORIZONTAL, IconID::CONSTRAINT_HORIZONTAL},
            {CT::VERTICAL, IconID::CONSTRAINT_VERTICAL},
            {CT::POINTS_COINCIDENT, IconID::CONSTRAINT_POINTS_COINCIDENT},
            {CT::POINT_ON_LINE, IconID::CONSTRAINT_POINT_ON_LINE},
            {CT::POINT_ON_CIRCLE, IconID::CONSTRAINT_POINT_ON_CIRCLE},
            {CT::MIDPOINT, IconID::CONSTRAINT_MIDPOINT},
            {CT::ARC_LINE_TANGENT, IconID::CONSTRAINT_ARC_LINE_TANGENT},
            {CT::ARC_ARC_TANGENT, IconID::CONSTRAINT_ARC_ARC_TANGENT},
            {CT::PARALLEL, IconID::CONSTRAINT_PARALLEL},
            {CT::POINT_ON_BEZIER, IconID::CONSTRAINT_POINT_ON_BEZIER},
            {CT::LOCK_ROTATION, IconID::CONSTRAINT_LOCK_ROTATION},
    };
    for (const auto constraint : constraints) {
        if (!constraint_icon_map.contains(constraint))
            continue;
        auto icon = constraint_icon_map.at(constraint);
        if (!std::isnan(v.x) && constraint == CT::VERTICAL)
            icon = IconID::CONSTRAINT_HORIZONTAL;
        add_constraint(p, icon, {}, v);
    }
}


void Renderer::add_constraint(const glm::vec3 &pos, IconTexture::IconTextureID icon, const UUID &constraint,
                              const glm::vec3 &v)
{
    for (auto &[p, l] : m_constraints) {
        if (glm::length(p - pos) < 1e-6) {
            l.push_back({icon, v, constraint});
            return;
        }
    }
    m_constraints.emplace_back();
    m_constraints.back().first = pos;
    m_constraints.back().second.push_back(ConstraintInfo{icon, v, constraint});
}

void Renderer::draw_constraints()
{
    AutoSaveRestore asr{*this};

    m_ca.set_vertex_constraint(true);
    for (const auto &[pos, constraints] : m_constraints) {
        double n = constraints.size();
        double spacing = 1;
        double offset = -(n - 1) / 2 * spacing;
        glm::vec3 v = {NAN, NAN, NAN};
        for (const auto &constraint : constraints) {
            if (!std::isnan(constraint.v.x) && (glm::length(constraint.v) != 0)) {
                v = constraint.v;
                break;
            }
        }
        for (const auto &constraint : constraints) {
            AutoSaveRestore asr2{*this};

            m_ca.set_selection_invisible(!constraint.constraint);
            const auto vr = m_ca.draw_icon(constraint.icon, pos, glm::vec2(offset, -.9), v);

            if (constraint.constraint)
                m_ca.add_selectable(vr, SelectableRef{SelectableRef::Type::CONSTRAINT, constraint.constraint, 0});
            offset += spacing;
        }
    }
}

void Renderer::save(Badge<AutoSaveRestore>)
{
    m_states.push_back(m_state);
    m_ca.save();
}

void Renderer::restore(Badge<AutoSaveRestore>)
{
    if (m_states.size() == 0)
        throw std::runtime_error("restore from empty states");

    m_state = m_states.back();
    m_states.pop_back();
    m_ca.restore();
}

unsigned int Renderer::get_chunk_from_group(const Group &group)
{
    return group.get_index() + 1;
}

void Renderer::set_chunk_from_group(const Group &group)
{
    if (m_is_current_document)
        m_ca.set_chunk(get_chunk_from_group(group));
}

} // namespace dune3d
