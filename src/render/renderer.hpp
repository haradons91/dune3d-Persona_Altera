#pragma once
#include "util/uuid.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <list>
#include <vector>
#include "document/group/all_groups_fwd.hpp"
#include "document/entity/entity_visitor.hpp"
#include "document/constraint/constraint_visitor.hpp"
#include "canvas/icanvas.hpp"
#include "util/badge.hpp"
#include <optional>
#include <filesystem>
#include <set>

namespace dune3d {

namespace IconTexture {
enum class IconTextureID;
}

class ICanvas;
class IDocumentProvider;
class Document;
class IDocumentView;
class IWorkspaceView;
class SelectableRef;
enum class ConstraintType;

class Renderer : private EntityVisitor, private ConstraintVisitor {
public:
    Renderer(ICanvas &ca, IDocumentProvider &docprv);
    // component_registry/accum_origin/accum_rot/occurrence_path/occurrence_active_stack
    // are only ever passed explicitly by Renderer::visit(const EntityOccurrence&)
    // when it recurses into a placed Component's own Document -- every other
    // caller (the top-level render pass, EntityDocument's linked-document
    // recursion) uses the defaults, which correctly mean "this Document is
    // its own component registry, with no accumulated occurrence transform."
    void render(const Document &doc, const UUID &current_group, const IDocumentView &doc_view,
                const IWorkspaceView &wrk_view, const std::filesystem::path &containing_dir,
                std::optional<SelectableRef> sr, const Document *component_registry = nullptr,
                glm::dvec3 accum_origin = {0, 0, 0},
                glm::dquat accum_rot = glm::quat_identity<double, glm::defaultp>(),
                std::vector<UUID> occurrence_path = {}, std::vector<UUID> occurrence_active_stack = {},
                UUID folder_key = {});

    // The occurrence path the user is currently "inside" for editing
    // purposes (Core::get_active_occurrence_path()) -- content whose own
    // recursion path (m_occurrence_path) isn't this path or a descendant of
    // it gets dimmed (see render(const Entity&)'s dimming predicate), same
    // idea as the existing "dim groups other than the current one" check,
    // generalized across occurrence boundaries. Not a render()
    // parameter: it's set once by the caller (like
    // m_render_extrusion_editor and friends) and copied verbatim onto each
    // nested Renderer that EntityOccurrence recursion constructs, since it
    // names a fixed target for the whole pass rather than something that
    // changes with recursion depth the way m_occurrence_path does.
    std::vector<UUID> m_active_occurrence_path;

    bool m_solid_model_edge_select_mode = false;
    bool m_connect_curvature_comb = true;
    bool m_render_sketch_plane_selector = false;
    bool m_render_sketch_grid = false;
    bool m_render_attached_sketch_body_transparent = false;
    bool m_render_extrusion_editor = false;
    bool m_show_dimension_points = false;
    std::optional<UUID> m_sketch_plane_hovered;
    std::optional<UUID> m_sketch_plane_grid;
    std::optional<glm::dvec3> m_sketch_grid_offset;
    UUID m_first_group;
    // Profile indices selected in the sketch.  These are passed in by the
    // editor so the profile overlay does not visually cover every profile.
    std::optional<UUID> m_selected_sketch_profile_group;
    std::set<unsigned int> m_selected_sketch_profiles;

    void add_constraint_icons(glm::vec3 p, glm::vec3 v, const std::vector<ConstraintType> &constraints);
    static unsigned int get_chunk_from_group(const Group &group);

private:
    void render(const Entity &en);
    void visit(const EntityLine3D &en) override;
    void visit(const EntityLine2D &en) override;
    void visit(const EntityArc2D &en) override;
    void visit(const EntityArc3D &en) override;
    void visit(const EntityCircle2D &en) override;
    void visit(const EntityCircle3D &en) override;
    void visit(const EntityWorkplane &en) override;
    void draw_sketch_grid(const EntityWorkplane &wrkpl);
    void visit(const EntitySTEP &en) override;
    void visit(const EntitySTL &en) override;
    void visit(const EntityThreeMF &en) override;
    void visit(const EntityPoint2D &en) override;
    void visit(const EntityDocument &en) override;
    void visit(const EntityBezier2D &en) override;
    void visit(const EntityBezier3D &en) override;
    void visit(const EntityCluster &en) override;
    void visit(const EntityText &en) override;
    void visit(const EntityPicture &en) override;
    void visit(const EntityOccurrence &en) override;
    void visit(const ConstraintPointDistance &constr) override;
    void visit(const ConstraintPointDistanceHV &constr) override;
    void visit(const ConstraintPointsCoincident &constr) override;
    void visit(const ConstraintHV &constr) override;
    void visit(const ConstraintPointOnLine &constr) override;
    void visit(const ConstraintPointOnCircle &constr) override;
    void visit(const ConstraintWorkplaneNormal &constr) override;
    void visit(const ConstraintMidpoint &constr) override;
    void visit(const ConstraintParallel &constr) override;
    void visit(const ConstraintSameOrientation &constr) override;
    void visit(const ConstraintEqualLength &constr) override;
    void visit(const ConstraintLengthRatio &constr) override;
    void visit(const ConstraintEqualRadius &constr) override;
    void visit(const ConstraintDiameterRadius &constr) override;
    void visit(const ConstraintArcArcTangent &constr) override;
    void visit(const ConstraintArcArcTangent &constr, IconTexture::IconTextureID icon);
    void visit(const ConstraintArcLineTangent &constr) override;
    void visit(const ConstraintLinePointsPerpendicular &constr) override;
    void visit(const ConstraintLinesPerpendicular &constr) override;
    void visit(const ConstraintLinesAngle &constr) override;
    void visit(const ConstraintPointInPlane &constr) override;
    void visit(const ConstraintPointLineDistance &constr) override;
    void visit(const ConstraintPointPlaneDistance &constr) override;
    void visit(const ConstraintLockRotation &constr) override;
    void visit(const ConstraintPointInWorkplane &constr) override;
    void visit(const ConstraintSymmetricHV &constr) override;
    void visit(const ConstraintSymmetricLine &constr) override;
    void visit(const ConstraintPointDistanceAligned &constr) override;
    void visit(const ConstraintBezierLineTangent &constr) override;
    void visit(const ConstraintBezierBezierTangentSymmetric &constr) override;
    void visit(const ConstraintPointOnBezier &constr) override;
    void visit(const ConstraintPointOnBezier &constr, IconTexture::IconTextureID icon);
    void visit(const ConstraintLineTangentOnBezier &constr) override;
    void visit(const ConstraintLinePerpendicularOnBezier &constr) override;
    void visit(const ConstraintBezierBezierSameCurvature &constr) override;
    void visit(const ConstraintBezierArcSameCurvature &constr) override;

    ICanvas &m_ca;
    IDocumentProvider &m_doc_prv;
    const Document *m_doc = nullptr;
    const IDocumentView *m_doc_view = nullptr;
    const IWorkspaceView *m_workspace_view = nullptr;
    const Group *m_current_group = nullptr;
    const Group *m_current_body_group = nullptr;
    std::filesystem::path m_containing_dir;
    bool m_is_current_document = true;
    UUID m_document_uuid;

    // Occurrence recursion state -- see the render() overload above and
    // visit(const EntityOccurrence&). m_component_registry is always the
    // root Document that owns m_components (never whatever Document is
    // currently being iterated); m_accum_origin/m_accum_rot are the
    // world-space placement accumulated across however many nested
    // occurrences got us here, composed in double precision so deeply
    // nested, large-coordinate scenes don't reintroduce the float32
    // precision loss the floating-origin fix (Canvas::m_render_origin)
    // addressed for the single-level case.
    const Document *m_component_registry = nullptr;
    glm::dvec3 m_accum_origin = {0, 0, 0};
    glm::dquat m_accum_rot = glm::quat_identity<double, glm::defaultp>();
    std::vector<UUID> m_occurrence_path;
    std::vector<UUID> m_occurrence_active_stack;
    // Which Document's "Sketches"/"Meshes" folder checkbox governs the
    // sketch/mesh groups currently being rendered -- see
    // IDocumentView::sketch_folder_is_visible()/mesh_folder_is_visible().
    UUID m_folder_key;

    bool group_is_visible(const UUID &uu) const;

    struct ConstraintInfo {
        IconTexture::IconTextureID icon;
        glm::vec3 v;
        UUID constraint;
    };
    std::list<std::pair<glm::vec3, std::list<ConstraintInfo>>> m_constraints;

    void add_constraint(const glm::vec3 &pos, IconTexture::IconTextureID icon, const UUID &constraint,
                        const glm::vec3 &v = {NAN, NAN, NAN});
    void draw_constraints();

    void draw_distance_line(const glm::vec3 &from, const glm::vec3 &to, const glm::vec3 &text_p,
                            const std::string &label, const UUID &uu,
                            const glm::vec3 &fallback_normal = {NAN, NAN, NAN});
    void draw_distance_line_with_direction(const glm::vec3 &from, const glm::vec3 &to, const glm::vec3 &dir,
                                           const glm::vec3 &text_p, const std::string &label, const UUID &uu,
                                           const glm::vec3 &fallback_normal = {NAN, NAN, NAN});
    void add_selectables(const SelectableRef &sr, const std::vector<ICanvas::VertexRef> &vrs);

    struct State {
        bool no_bezier_control_lines = false;
        bool no_curvature_combs = false;
    };

    State m_state;
    std::vector<State> m_states;

    class AutoSaveRestore;

    void save(Badge<AutoSaveRestore>);
    void restore(Badge<AutoSaveRestore>);

    void set_chunk_from_group(const Group &group);
    glm::dvec3 get_sketch_geometry_offset() const;

    float m_curvature_comb_scale = 0;
};

} // namespace dune3d
