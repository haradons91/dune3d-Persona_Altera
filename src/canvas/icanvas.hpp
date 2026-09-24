#pragma once
#include <glm/glm.hpp>
#include <tuple>
#include "face.hpp"
#include <glm/gtx/quaternion.hpp>
#include <memory>
#include <vector>
#include "util/uuid.hpp"

namespace dune3d {

class SelectableRef;
class PictureData;

namespace IconTexture {
enum class IconTextureID;
}

class ICanvas {
public:
    enum class VertexType { LINE, GLYPH, GLYPH_3D, ICON, FACE_GROUP, PICTURE, SELECTION_INVISIBLE };
    struct VertexRef {
        VertexType type;
        size_t index;
        unsigned int chunk;

        friend auto operator<=>(const VertexRef &, const VertexRef &) = default;
        friend bool operator==(const VertexRef &, const VertexRef &) = default;
    };

    enum class LineStyle {
        DEFAULT = 0,
        THIN = (1 << 0),
        THINNER = (1 << 1),
    };

    enum class Axis {
        NONE = 0,
        Y = 1,
        X = 2,
        Z = 3,
        PLANE = 4,
        PLANE_HIGHLIGHT = 5,
    };

    virtual void set_chunk(unsigned int chunk) = 0;
    virtual glm::dvec2 project_to_window(glm::dvec3 point) const = 0;

    virtual void clear() = 0;
    virtual VertexRef draw_point(glm::vec3 p) = 0;
    // Lines take double precision: at extreme zoom the camera sits close to
    // geometry that may be far from the world origin, and float32 world
    // coordinates lose enough precision in the view transform to visibly
    // thin out (or misplace) the line-width offset computed in the
    // geometry shader. See Canvas::transform_point/m_render_origin.
    virtual VertexRef draw_line(glm::dvec3 from, glm::dvec3 to) = 0;
    virtual VertexRef draw_axis_line(glm::dvec3 from, glm::dvec3 to, Axis axis) = 0;
    virtual VertexRef draw_screen_line(glm::vec3 origin, glm::vec3 direction) = 0;
    virtual std::vector<VertexRef> draw_bitmap_text(glm::vec3 p, float size, const std::string &rtext) = 0;
    virtual std::vector<VertexRef> draw_bitmap_text_centered(glm::vec3 p, float size,
                                                             const std::string &rtext, float angle = 0) = 0;
    virtual std::vector<VertexRef> draw_bitmap_text_3d(glm::vec3 p, const glm::quat &norm, float size,
                                                       const std::string &rtext) = 0;

    // virtual void add_faces(const face::Faces &faces) = 0;
    enum class FaceColor {
        AS_IS,
        SOLID_MODEL,
        SOLID_MODEL_TRANSPARENT,
        OTHER_BODY_SOLID_MODEL,
        SKETCH_PLANE,
        SKETCH_PLANE_HIGHLIGHT,
        SKETCH_PROFILE
    };
    virtual VertexRef add_face_group(const face::Faces &faces, glm::vec3 origin, glm::quat normal,
                                     FaceColor face_color) = 0;
    virtual VertexRef draw_icon(IconTexture::IconTextureID id, glm::vec3 origin, glm::vec2 shift,
                                glm::vec3 v = {NAN, NAN, NAN}) = 0;
    virtual VertexRef draw_point(glm::vec3 origin, IconTexture::IconTextureID id) = 0;
    virtual VertexRef draw_picture(const std::array<glm::vec3, 4> &corners,
                                   std::shared_ptr<const PictureData> data) = 0;

    virtual void add_selectable(const VertexRef &vref, const SelectableRef &sref) = 0;
    virtual void add_hover_selectable(const VertexRef &vref, const SelectableRef &sref) = 0;
    virtual void set_selection_invisible(bool selection_invisible) = 0;

    virtual void save() = 0;
    virtual void restore() = 0;

    // tracked by save/restore
    virtual void set_vertex_inactive(bool inactive) = 0;
    virtual void set_vertex_hover_only(bool hover_only) = 0;
    virtual void set_vertex_constraint(bool c) = 0;
    virtual void set_vertex_construction(bool c) = 0;
    virtual void set_no_points(bool c) = 0;
    virtual void set_show_default_points(bool c) = 0;
    virtual void set_line_style(LineStyle style) = 0;
    virtual void set_transform(const glm::mat4 &transform) = 0;
    // Like set_transform, but the translation is a double-precision origin
    // composed separately from the (rotation-only) matrix -- see
    // Canvas::transform_point. Used for occurrence placement, where the
    // translation may be at a large world coordinate reached through many
    // nested occurrences, and narrowing it to float before it's combined
    // with m_render_origin would reintroduce the precision loss the
    // floating-origin fix addressed for ordinary geometry.
    virtual void set_transform_d(const glm::mat4 &rotation, const glm::dvec3 &origin) = 0;

    virtual void set_override_selectable(const SelectableRef &sr) = 0;
    virtual void unset_override_selectable() = 0;

    // Like set_override_selectable, but tags newly-added selectables with
    // an occurrence path instead of collapsing them to a single ref --
    // used by occurrence rendering (Renderer::visit(const EntityOccurrence&))
    // so individual entities inside a placed component stay individually
    // selectable (in the reported-location sense; edit-in-place is a later
    // milestone), unlike EntityCluster/EntityDocument's existing
    // collapse-to-one-ref behavior via set_override_selectable. Nestable:
    // each call pushes a (deeper) path; clear pops it, so returning to an
    // outer occurrence's render call restores its own (shallower) path.
    virtual void set_occurrence_path(const std::vector<UUID> &path) = 0;
    virtual void clear_occurrence_path() = 0;

    virtual void update_bbox() = 0;
    virtual glm::vec3 get_cam_normal() const = 0;
    virtual glm::vec3 get_cam_center() const = 0;
    virtual float get_cam_distance() const = 0;
    virtual float get_world_units_per_pixel() const = 0;
    virtual glm::vec2 get_viewport_size() const = 0;
};
} // namespace dune3d
