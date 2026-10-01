#include "axes_cube.hpp"
#include "util/color.hpp"
#include <glm/gtx/rotate_vector.hpp>
#include <vector>
#include <string>
#include <algorithm>

namespace dune3d {

static const std::array<std::string, 3> s_xyz = {"X", "Y", "Z"};

static const Color &get_color(unsigned int ax, float z)
{
    static const std::array<Color, 3> ax_colors_pos = {
            Color::new_from_int(255, 54, 83),
            Color::new_from_int(138, 219, 0),
            Color::new_from_int(44, 142, 254),
    };
    static const std::array<Color, 3> ax_colors_neg = {
            Color::new_from_int(155, 57, 7),
            Color::new_from_int(98, 137, 34),
            Color::new_from_int(51, 100, 155),
    };
    if (z >= -.001)
        return ax_colors_pos.at(ax);
    else
        return ax_colors_neg.at(ax);
}

AxesCube::AxesCube()
{
    create_layout();
    for (unsigned int ax = 0; ax < 3; ax++) {
        m_layout->set_text(s_xyz.at(ax));
        auto ext = m_layout->get_pixel_logical_extents();
        m_size = std::max(m_size, (float)ext.get_width());
        m_size = std::max(m_size, (float)ext.get_height());
    }
    set_content_height(100);
    set_content_width(140);
    set_draw_func(sigc::mem_fun(*this, &AxesCube::render));
    setup_controllers();
}

void AxesCube::create_layout()
{
    m_layout = create_pango_layout("");
    Pango::AttrList attrs;
    auto attr = Pango::Attribute::create_attr_weight(Pango::Weight::BOLD);
    attrs.insert(attr);
    m_layout->set_attributes(attrs);
}

void AxesCube::set_quat(const glm::quat &q)
{
    m_quat = q;
    queue_draw();
}

sigc::signal<void(const glm::quat &)> AxesCube::signal_quat_changed()
{
    return m_signal_quat_changed;
}

sigc::signal<void(float)> AxesCube::signal_roll_changed()
{
    return m_signal_roll_changed;
}

sigc::signal<void()> AxesCube::signal_home_clicked()
{
    return m_signal_home_clicked;
}

namespace {
struct Face {
    std::vector<int> vertices;
    int id;
    std::string name;
    Color color;
    std::string label;
    std::optional<glm::quat> target_quat;
    // In-plane unit directions (model space) for drawing this face's label so
    // it reads upright when the face is viewed dead-on, and otherwise
    // rotates/shears with the face like a decal. Unused (zero) for faces
    // with no label.
    glm::vec3 text_right{0.0f};
    glm::vec3 text_down{0.0f};
};

struct Model {
    std::vector<glm::vec3> vertices;
    std::vector<Face> faces;
};

// Half-width of each of the six main faces in model space (see the vertex
// generation below); render() needs this too, to size labels against the
// face's actual on-screen footprint.
static constexpr double FACE_HALF_WIDTH = 0.60;

// Determines text_right/text_down for a labeled quad face from a single,
// consistent "world up" reference (the same convention already used for the
// corner-click views below: +Z, except on the Top/Bottom faces themselves,
// where the face normal IS +-Z and +Y is used instead). Using one shared
// reference for every face -- rather than each face's own locally-"upright"
// edge, which is only unambiguous up to a 90-degree choice -- is what makes
// Left/Right/Front/Back/Top/Bottom's labels all agree with each other in a
// typical rotated (not dead-on) view, like the isometric default: they were
// derived from a per-face-only test before, and while each individually
// looked upright head-on, Left/Right came out rotated 90 degrees from
// Top/Front/Back/Bottom in every other view. The face's own target_quat is
// still used, but only to sign-correct (not choose) each axis, by checking
// it points screen-right / screen-down at that dead-on view.
void compute_text_axes(const std::vector<glm::vec3> &verts, const std::vector<int> &idx, const glm::quat &target_quat,
                       glm::vec3 &text_right, glm::vec3 &text_down)
{
    glm::vec3 normal =
            glm::cross(verts[idx[1]] - verts[idx[0]], verts[idx[2]] - verts[idx[0]]);
    glm::vec3 center{0.0f};
    for (int i : idx)
        center += verts[i];
    center /= static_cast<float>(idx.size());
    if (glm::dot(normal, center) < 0)
        normal = -normal;
    normal = glm::normalize(normal);

    const glm::vec3 world_up = (std::abs(normal.z) > 0.9f) ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
    text_right = glm::normalize(glm::cross(world_up, normal));
    text_down = glm::normalize(glm::cross(normal, text_right));

    const glm::quat view = glm::angleAxis(glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)) * glm::inverse(target_quat);
    if (glm::rotate(view, text_right).x < 0)
        text_right = -text_right;
    if (glm::rotate(view, text_down).y < 0)
        text_down = -text_down;
}
} // namespace

static Model generate_model()
{
    constexpr double S = 0.9;
    constexpr double B = FACE_HALF_WIDTH;

    std::vector<glm::vec3> vertices;
    vertices.reserve(24);
    std::vector<Face> faces;
    faces.reserve(26);

    // generate vertices
    for (int i = 0; i < 8; ++i) {
        double sx = (i & 1) ? 1.0 : -1.0;
        double sy = (i & 2) ? 1.0 : -1.0;
        double sz = (i & 4) ? 1.0 : -1.0;

        vertices.push_back({sx * S, sy * B, sz * B}); // 0: X-side
        vertices.push_back({sx * B, sy * S, sz * B}); // 1: Y-side
        vertices.push_back({sx * B, sy * B, sz * S}); // 2: Z-side
    }

    int id = 0;
    Color col_gray = Color::new_from_int(200, 200, 200);

    auto add_face = [&](std::vector<int> idxs, std::string name, Color c, std::string lbl = "",
                        glm::quat quat = glm::quat()) {
        glm::vec3 text_right{0.0f}, text_down{0.0f};
        if (!lbl.empty() && idxs.size() == 4) {
            compute_text_axes(vertices, idxs, quat, text_right, text_down);
        }
        faces.push_back({std::move(idxs), id++, std::move(name), c, std::move(lbl),
                         std::make_optional(std::move(quat)), text_right, text_down});
    };

    // faces
    add_face({1 * 3 + 0, 5 * 3 + 0, 7 * 3 + 0, 3 * 3 + 0}, "−X", get_color(0, -1.0f), "Left",
             glm::quat(glm::vec3(0, -glm::pi<float>() / 2, 0)));
    add_face({0 * 3 + 0, 2 * 3 + 0, 6 * 3 + 0, 4 * 3 + 0}, "+X", get_color(0, 1.0f), "Right",
             glm::quat(glm::vec3(0, glm::pi<float>() / 2, 0)));
    add_face({2 * 3 + 1, 3 * 3 + 1, 7 * 3 + 1, 6 * 3 + 1}, "−Y", get_color(1, -1.0f), "Front",
             glm::quat(glm::vec3(glm::pi<float>() / 2, 0, 0)));
    add_face({0 * 3 + 1, 4 * 3 + 1, 5 * 3 + 1, 1 * 3 + 1}, "+Y", get_color(1, 1.0f), "Back",
             glm::quat(glm::vec3(-glm::pi<float>() / 2, 0, 0)));
    add_face({4 * 3 + 2, 6 * 3 + 2, 7 * 3 + 2, 5 * 3 + 2}, "−Z", get_color(2, -1.0f), "Bottom",
             glm::quat(glm::vec3(0, glm::pi<float>(), 0)));
    add_face({0 * 3 + 2, 1 * 3 + 2, 3 * 3 + 2, 2 * 3 + 2}, "+Z", get_color(2, 1.0f), "Top", glm::quat(1, 0, 0, 0));

    // corners
    for (int i = 0; i < 8; ++i) {
        double sx = (i & 1) ? 1.0 : -1.0;
        double sy = (i & 2) ? 1.0 : -1.0;
        double sz = (i & 4) ? 1.0 : -1.0;
        std::string sname = "Corner " + std::string(sx > 0 ? "+X" : "-X") + std::string(sy > 0 ? "+Y" : "-Y")
                            + std::string(sz > 0 ? "+Z" : "-Z");

        // isometric views looking toward the corner
        glm::vec3 target_dir(sx, sy, sz);
        glm::vec3 up(0, 0, 1);
        if (std::abs(target_dir.z) > 0.9) {
            up = glm::vec3(0, 1, 0);
        }
        glm::quat corner_quat = glm::quatLookAt(glm::normalize(target_dir), up);

        if (sx * sy * sz > 0)
            add_face({i * 3 + 0, i * 3 + 2, i * 3 + 1}, sname, col_gray, "", corner_quat);
        else
            add_face({i * 3 + 2, i * 3 + 0, i * 3 + 1}, sname, col_gray, "", corner_quat);
    }

    // X-axis edges
    add_face({6 * 3 + 1, 7 * 3 + 1, 7 * 3 + 2, 6 * 3 + 2}, "Edge −Y−Z", col_gray, "",
             glm::quat(glm::vec3(glm::pi<float>() / 4, glm::pi<float>() / 1, 0)));
    add_face({2 * 3 + 2, 3 * 3 + 2, 3 * 3 + 1, 2 * 3 + 1}, "Edge −Y+Z", col_gray, "",
             glm::quat(glm::vec3(glm::pi<float>() / 4, 0, 0)));
    add_face({4 * 3 + 2, 5 * 3 + 2, 5 * 3 + 1, 4 * 3 + 1}, "Edge +Y−Z", col_gray, "",
             glm::quat(glm::vec3(-glm::pi<float>() / 4, glm::pi<float>() / 1, 0)));
    add_face({0 * 3 + 1, 1 * 3 + 1, 1 * 3 + 2, 0 * 3 + 2}, "Edge +Y+Z", col_gray, "",
             glm::quat(glm::vec3(-glm::pi<float>() / 4, 0, 0)));

    // Y-axis edges
    add_face({5 * 3 + 2, 7 * 3 + 2, 7 * 3 + 0, 5 * 3 + 0}, "Edge −X−Z", col_gray, "",
             glm::quat(glm::vec3(0, -glm::pi<float>() * 3 / 4, 0)));
    add_face({1 * 3 + 0, 3 * 3 + 0, 3 * 3 + 2, 1 * 3 + 2}, "Edge −X+Z", col_gray, "",
             glm::quat(glm::vec3(0, -glm::pi<float>() / 4, 0)));
    add_face({4 * 3 + 0, 6 * 3 + 0, 6 * 3 + 2, 4 * 3 + 2}, "Edge +X−Z", col_gray, "",
             glm::quat(glm::vec3(0, glm::pi<float>() * 3 / 4, 0)));
    add_face({0 * 3 + 2, 2 * 3 + 2, 2 * 3 + 0, 0 * 3 + 0}, "Edge +X+Z", col_gray, "",
             glm::quat(glm::vec3(0, glm::pi<float>() / 4, 0)));

    // Z-axis edges
    add_face({3 * 3 + 0, 7 * 3 + 0, 7 * 3 + 1, 3 * 3 + 1}, "Edge −X−Y", col_gray, "",
             glm::quat(glm::vec3(glm::pi<float>() / 4, -glm::pi<float>() / 2, 0)));
    add_face({1 * 3 + 1, 5 * 3 + 1, 5 * 3 + 0, 1 * 3 + 0}, "Edge −X+Y", col_gray, "",
             glm::quat(glm::vec3(-glm::pi<float>() / 4, -glm::pi<float>() / 2, 0)));
    add_face({2 * 3 + 1, 6 * 3 + 1, 6 * 3 + 0, 2 * 3 + 0}, "Edge +X−Y", col_gray, "",
             glm::quat(glm::vec3(0, glm::pi<float>() / 2, -glm::pi<float>() / 4)));
    add_face({0 * 3 + 0, 4 * 3 + 0, 4 * 3 + 1, 0 * 3 + 1}, "Edge +X+Y", col_gray, "",
             glm::quat(glm::vec3(0, glm::pi<float>() / 2, glm::pi<float>() / 4)));

    // Axis vertices (+X+Y+Z corner only)
    constexpr float S_axis = 0.9f;
    constexpr float offset_factor = 1.08f;
    constexpr float axis_length = 0.55f;
    const float sx = 1.0f, sy = 1.0f, sz = 1.0f;
    glm::vec3 origin = glm::vec3(sx * S_axis, sy * S_axis, sz * S_axis) * offset_factor;
    vertices.push_back(origin);
    vertices.push_back(origin - glm::vec3(sx * axis_length, 0.0f, 0.0f));
    vertices.push_back(origin - glm::vec3(0.0f, sy * axis_length, 0.0f));
    vertices.push_back(origin - glm::vec3(0.0f, 0.0f, sz * axis_length));

    return Model{vertices, faces};
}

static const auto &get_cached_model()
{
    static const auto model = generate_model();
    return model;
}

static bool is_face_visible(const std::vector<glm::vec3> &m_transformed_vertices, const std::vector<int> &face_vertices)
{
    if (face_vertices.size() < 3)
        return false;
    const glm::vec3 &v0 = m_transformed_vertices[face_vertices[0]];
    const glm::vec3 &v1 = m_transformed_vertices[face_vertices[1]];
    const glm::vec3 &v2 = m_transformed_vertices[face_vertices[2]];
    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 normal = glm::cross(edge1, edge2);
    return normal.z < 0;
}

void AxesCube::update_transformed_vertices()
{
    const glm::quat corrected_view_quat =
            glm::angleAxis(glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)) * glm::inverse(m_quat);

    if (m_transformed_vertices.empty() || m_quat != m_cached_quat || m_width != m_cached_width
        || m_height != m_cached_height) {

        const auto &vertices = get_cached_model().vertices;
        const float sc = (std::min(m_width, m_height) / 2.0f) - m_size;

        if (m_transformed_vertices.size() != vertices.size()) {
            m_transformed_vertices.resize(vertices.size());
        }

        for (size_t i = 0; i < vertices.size(); i++) {
            m_transformed_vertices[i] = glm::rotate(corrected_view_quat, vertices[i]) * sc;
        }

        m_cached_quat = m_quat;
        m_cached_width = m_width;
        m_cached_height = m_height;

        // Changing the position of the vertices within the widget can change what the mouse pointer hovers without its
        // position changing (and without the widget getting any motion events).
        update_hover_effect();
    }
}

void AxesCube::setup_controllers()
{
    auto motion_controller = Gtk::EventControllerMotion::create();
    motion_controller->signal_motion().connect([this](double x, double y) {
        m_last_x = x;
        m_last_y = y;
        const bool home_hovered = x < 38 && y > m_height - 34;
        if (home_hovered != m_home_hovered) {
            m_home_hovered = home_hovered;
            queue_draw();
        }
        update_hover_effect();
    });
    motion_controller->signal_leave().connect([this] {
        m_home_hovered = false;
        if (m_hovered_face != -1) {
            m_hovered_face = -1;
            queue_draw();
        }
    });
    add_controller(motion_controller);

    auto click_controller = Gtk::GestureClick::create();
    click_controller->set_button(1);
    click_controller->signal_pressed().connect([this](int n_press, double x, double y) {
        if (x < 38 && y > m_height - 34) {
            m_signal_home_clicked.emit();
            return;
        }
        if (x < 52 && y < 34) {
            m_signal_roll_changed.emit(45.0f);
            return;
        }
        if (x > m_width - 52 && y < 34) {
            m_signal_roll_changed.emit(-45.0f);
            return;
        }
        int face_id = get_face_at_position(x, y);
        if (face_id >= 0) {
            const auto &faces = get_cached_model().faces;
            if (face_id < (int)faces.size()) {
                const auto &face = faces[face_id];

                if (face.target_quat) {
                    m_signal_quat_changed.emit(*face.target_quat);
                }
            }
        }
    });
    add_controller(click_controller);
}

void AxesCube::update_hover_effect()
{
    int old_hovered = m_hovered_face;
    m_hovered_face = get_face_at_position(m_last_x, m_last_y);
    if (old_hovered != m_hovered_face) {
        queue_draw();
    }
}

int AxesCube::get_face_at_position(double x, double y) const
{
    if (m_width == 0 || m_height == 0)
        return -1;

    const glm::vec2 p(x - m_width / 2.0f, y - m_height / 2.0f);
    const auto &faces = get_cached_model().faces;

    int best_face = -1;
    float min_z = 1e9f;

    for (const auto &face : faces) {
        if (!is_face_visible(m_transformed_vertices, face.vertices))
            continue;

        bool hit = true;
        const size_t n = face.vertices.size();
        for (size_t i = 0; i < n; i++) {
            const auto &v1 = m_transformed_vertices[face.vertices[i]];
            const auto &v2 = m_transformed_vertices[face.vertices[(i + 1) % n]];
            if ((v2.x - v1.x) * (p.y - v1.y) - (v2.y - v1.y) * (p.x - v1.x) > 0) {
                hit = false;
                break;
            }
        }

        if (hit) {
            float avg_z = 0;
            for (int idx : face.vertices)
                avg_z += m_transformed_vertices[idx].z;
            avg_z /= n;
            if (avg_z < min_z) {
                min_z = avg_z;
                best_face = face.id;
            }
        }
    }
    return best_face;
}

void AxesCube::render(const Cairo::RefPtr<Cairo::Context> &cr, int w, int h)
{
    // Debug aid: when set, dump this widget's own Cairo rendering straight
    // to a PNG on every redraw -- a ground-truth capture of exactly what
    // Cairo draws for this widget, independent of the window manager,
    // compositor, or any external OS-level screenshot tool (which may not
    // be in sync with the latest frame, particularly for GL-adjacent
    // widgets). The reentrancy guard keeps this recursive render() call
    // from triggering another snapshot of itself.
    static bool in_snapshot = false;
    if (!in_snapshot && getenv("DUNE3D_SNAPSHOT_AXES_CUBE")) {
        in_snapshot = true;
        // Temporary test-only override to snapshot an arbitrary view without
        // live UI interaction, e.g. "1 0 0 0" for Top's identity quat.
        if (const char *fq = getenv("DUNE3D_FORCE_QUAT")) {
            float qw, qx, qy, qz;
            if (sscanf(fq, "%f %f %f %f", &qw, &qx, &qy, &qz) == 4) {
                m_quat = glm::quat(qw, qx, qy, qz);
            }
        }
        auto surface = Cairo::ImageSurface::create(Cairo::Surface::Format::ARGB32, w, h);
        auto debug_cr = Cairo::Context::create(surface);
        render(debug_cr, w, h);
        surface->write_to_png("/tmp/axes_cube_snapshot.png");
        FILE *f = fopen("/tmp/axes_cube_quat.txt", "w");
        if (f) {
            fprintf(f, "%f %f %f %f\n", m_quat.w, m_quat.x, m_quat.y, m_quat.z);
            fclose(f);
        }
        in_snapshot = false;
    }

    m_width = w;
    m_height = h;

    update_transformed_vertices();

    cr->translate(w / 2.0, h / 2.0);
    cr->set_line_width(1.0);
    cr->set_line_join(Cairo::Context::LineJoin::ROUND);

    const auto &faces = get_cached_model().faces;

    // Same correction quat used in update_transformed_vertices() to turn
    // m_quat into vertex positions -- needed again here to rotate each
    // face's label-direction vectors the identical way.
    const glm::quat corrected_view_quat =
            glm::angleAxis(glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)) * glm::inverse(m_quat);
    // Same vertex scale factor used in update_transformed_vertices(); needed
    // again here to size labels against the face's actual pixel footprint.
    const float sc = (std::min(m_width, m_height) / 2.0f) - m_size;

    struct VisibleFace {
        const Face *face;
        float depth;
    };
    std::vector<VisibleFace> visible_faces;
    visible_faces.reserve(faces.size());

    for (const auto &face : faces) {
        if (is_face_visible(m_transformed_vertices, face.vertices)) {
            float avg_z = 0;
            for (int idx : face.vertices)
                avg_z += m_transformed_vertices[idx].z;
            avg_z /= face.vertices.size();
            visible_faces.push_back({&face, avg_z});
        }
    }

    std::sort(visible_faces.begin(), visible_faces.end(),
              [](const VisibleFace &a, const VisibleFace &b) { return a.depth < b.depth; });

    for (const auto &vf : visible_faces) {
        const Face &face = *vf.face;
        Color color = face.color;

        if (m_hovered_face == face.id) {
            color.r = std::min(1.0f, color.r * 1.4f);
            color.g = std::min(1.0f, color.g * 1.4f);
            color.b = std::min(1.0f, color.b * 1.4f);
        }

        const auto &v0 = m_transformed_vertices[face.vertices[0]];
        cr->move_to(v0.x, v0.y);
        for (size_t i = 1; i < face.vertices.size(); ++i) {
            const auto &v = m_transformed_vertices[face.vertices[i]];
            cr->line_to(v.x, v.y);
        }
        cr->close_path();

        cr->set_source_rgba(color.r, color.g, color.b, 0.7);
        cr->fill_preserve();

        cr->set_source_rgb(0, 0, 0);
        cr->stroke();

        if (!face.label.empty()) {
            m_layout->set_text(face.label);
            auto ext = m_layout->get_pixel_logical_extents();

            const glm::vec3 &v0 = m_transformed_vertices[face.vertices[0]];
            const glm::vec3 &v1 = m_transformed_vertices[face.vertices[1]];
            const glm::vec3 &v2 = m_transformed_vertices[face.vertices[2]];
            glm::vec3 edge1 = v1 - v0;
            glm::vec3 edge2 = v2 - v0;
            glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));
            float dot_prod_view = -normal.z;

            // Below this, the face is close enough to edge-on that its
            // decal-mapped text would be squashed to an illegible sliver;
            // fade it out rather than let it flicker/smear. Above the
            // steady threshold, full opacity -- actual size/shear still
            // comes from how foreshortened the face currently is, below.
            static const float LABEL_MIN_VISIBILITY_THRESHOLD = 0.1f;
            static const float LABEL_STEADY_DOT_PRODUCT_THRESHOLD = 0.5f;

            float alpha = 0.0f;
            if (dot_prod_view > LABEL_MIN_VISIBILITY_THRESHOLD) {
                if (dot_prod_view >= LABEL_STEADY_DOT_PRODUCT_THRESHOLD) {
                    alpha = 1.0f;
                }
                else {
                    alpha = (dot_prod_view - LABEL_MIN_VISIBILITY_THRESHOLD)
                            / (LABEL_STEADY_DOT_PRODUCT_THRESHOLD - LABEL_MIN_VISIBILITY_THRESHOLD);
                }
            }

            if (alpha > 0.01f) {
                float center_x = 0, center_y = 0;
                for (int idx : face.vertices) {
                    center_x += m_transformed_vertices[idx].x;
                    center_y += m_transformed_vertices[idx].y;
                }
                center_x /= face.vertices.size();
                center_y /= face.vertices.size();

                // Map the label's local pixel space onto the face's own
                // in-plane axes, rotated the same way every vertex is. A
                // unit-length in-plane direction keeps magnitude 1 (in 3D)
                // under rotation, so its screen-space (x, y) after dropping z
                // is exactly 1 at a dead-on view and shrinks with
                // foreshortening otherwise -- giving the label the same
                // rotation, shear, and foreshortening as the face itself,
                // like a decal, with no separate scale factor needed.
                const glm::vec3 right_dir = glm::rotate(corrected_view_quat, face.text_right);
                const glm::vec3 down_dir = glm::rotate(corrected_view_quat, face.text_down);

                // The matrix above alone only reproduces the face's rotation
                // and foreshortening; it doesn't know the label's own pixel
                // size versus the face's actual on-screen footprint, so a
                // long word (e.g. "Bottom") at this widget's tiny true scale
                // can still overflow past the face's edges even though the
                // orientation is correct. Shrink uniformly (never enlarge) so
                // the label's rendered width/height each fit within a margin
                // of the face's current screen-space extent along that axis.
                const double face_width_px =
                        2.0 * FACE_HALF_WIDTH * std::hypot(right_dir.x, right_dir.y) * sc;
                const double face_height_px =
                        2.0 * FACE_HALF_WIDTH * std::hypot(down_dir.x, down_dir.y) * sc;
                constexpr double LABEL_FIT_MARGIN = 0.8;
                double fit_scale = 1.0;
                if (ext.get_width() > 0)
                    fit_scale = std::min(fit_scale, face_width_px * LABEL_FIT_MARGIN / ext.get_width());
                if (ext.get_height() > 0)
                    fit_scale = std::min(fit_scale, face_height_px * LABEL_FIT_MARGIN / ext.get_height());
                fit_scale = std::max(fit_scale, 0.0);

                cr->save();
                Cairo::Matrix label_matrix(right_dir.x * fit_scale, right_dir.y * fit_scale, down_dir.x * fit_scale,
                                           down_dir.y * fit_scale, center_x, center_y);
                cr->transform(label_matrix);

                cr->set_source_rgba(0, 0, 0, alpha);
                cr->move_to(-ext.get_width() / 2.0, -ext.get_height() / 2.0);
                m_layout->show_in_cairo_context(cr);
                cr->restore();
            }
        }
    }

    // corner axes display
    const int AXIS_BASE = 8 * 3; // vertices[24..27]
    if ((int)m_transformed_vertices.size() >= AXIS_BASE + 4) {
        cr->set_line_width(2.0);
        cr->set_line_cap(Cairo::Context::LineCap::ROUND);

        const int base = AXIS_BASE;
        for (int ax = 0; ax < 3; ++ax) {
            const auto &o = m_transformed_vertices[base + 0];
            const auto &e = m_transformed_vertices[base + (ax + 1)];
            const Color &col = dune3d::get_color(ax, 1.0f);
            cr->set_source_rgba(col.r, col.g, col.b, 0.8f);
            cr->move_to(o.x, o.y);
            cr->line_to(e.x, e.y);
            cr->stroke();
        }
    }

    auto draw_roll_arrow = [&cr](double cx, double cy, bool left) {
        cr->save();
        cr->set_line_width(2.0);
        // The corner-axes block above sets LineCap::ROUND with no save/
        // restore of its own, so it's still active here -- a round cap on
        // the arc's open end (right at the arrowhead tip) drew a small
        // circular nub poking out past the chevron. Reset to a plain butt
        // cap for this clean geometric shape.
        cr->set_line_cap(Cairo::Context::LineCap::BUTT);
        cr->set_source_rgba(0.1, 0.1, 0.1, 0.85);
        // Build the whole shape in one canonical (clockwise/"right")
        // orientation, then mirror the x-axis for "left" by negating every
        // x-component (position AND direction vectors) after computing it
        // in that canonical frame. Re-deriving a separate set of angles/
        // signs for "left" by hand (the previous approach here) was a
        // repeated source of subtly-wrong arrowhead directions -- mirroring
        // a verified-correct shape is correct by construction instead.
        const double mirror = left ? -1.0 : 1.0;
        const double start = 2.35;
        const double end = 5.48;
        const double r = 8.0;
        const int n = 48;
        cr->move_to(cx + mirror * std::cos(start) * r, cy + std::sin(start) * r);
        for (int i = 1; i <= n; i++) {
            const double a = start + (end - start) * i / n;
            cr->line_to(cx + mirror * std::cos(a) * r, cy + std::sin(a) * r);
        }
        cr->stroke();

        const double tx = cx + mirror * std::cos(end) * r;
        const double ty = cy + std::sin(end) * r;
        // Forward tangent at the arc's end, in the canonical (unmirrored)
        // frame, then mirrored like everything else.
        const double tangent_angle = end + glm::half_pi<double>();
        const double tanx = mirror * std::cos(tangent_angle);
        const double tany = std::sin(tangent_angle);
        const double perp_angle = tangent_angle + glm::half_pi<double>();
        const double perpx = mirror * std::cos(perp_angle);
        const double perpy = std::sin(perp_angle);
        // Arrowhead as a base perpendicular to the tangent, sitting right at
        // the arc's endpoint, plus a tip that projects forward from there
        // along the tangent (height H past the endpoint, half-width W to
        // each side at the base). The tip must be the part that floats free
        // in open space beyond the stroke -- putting it AT the endpoint
        // instead (with the base projecting backward over the stroke, as an
        // earlier version of this code did) buries the point inside the
        // line and leaves only the forked base sticking out, which reads as
        // a hook, not an arrowhead. Verified against isolated 1:1-scale
        // Python/PIL renders before settling on these values.
        const double H = 6.0;
        const double W = 3.0;
        const double tip_x = tx + tanx * H;
        const double tip_y = ty + tany * H;
        cr->move_to(tip_x, tip_y);
        cr->line_to(tx + perpx * W, ty + perpy * W);
        cr->line_to(tx - perpx * W, ty - perpy * W);
        cr->close_path();
        cr->fill();
        cr->restore();
    };
    draw_roll_arrow(-34, -34, true);
    draw_roll_arrow(34, -34, false);

    // Home/default-view control in the lower-left corner.
    const double home_x = -w / 2.0 + 18;
    const double home_y = h / 2.0 - 17;
    const double house_width = 15;
    const double house_height = 12;
    const double roof_height = 7;
    cr->save();
    cr->set_line_width(1.2);
    cr->set_line_join(Cairo::Context::LineJoin::ROUND);
    cr->move_to(home_x - house_width / 2, home_y - house_height / 2 + roof_height);
    cr->line_to(home_x, home_y - house_height / 2);
    cr->line_to(home_x + house_width / 2, home_y - house_height / 2 + roof_height);
    cr->line_to(home_x + house_width / 2, home_y + house_height / 2);
    cr->line_to(home_x - house_width / 2, home_y + house_height / 2);
    cr->close_path();
    if (m_home_hovered)
        cr->set_source_rgba(0.35, 0.35, 0.35, 0.95);
    else
        cr->set_source_rgba(0.1, 0.1, 0.1, 0.78);
    cr->fill_preserve();
    cr->set_source_rgb(0, 0, 0);
    cr->stroke();
    cr->restore();
}

} // namespace dune3d
