#pragma once
#include "entityt.hpp"
#include <glm/glm.hpp>
#include "ientity_normal.hpp"
#include "ientity_movable3d.hpp"
#include "ientity_delete_point.hpp"
#include <filesystem>
#include "import_stl/imported_stl.hpp"

namespace dune3d {
class EntitySTL : public EntityT<EntitySTL>,
                  public IEntityNormal,
                  public IEntityMovable3D,
                  public IEntityDeletePoint {
public:
    explicit EntitySTL(const UUID &uu);
    explicit EntitySTL(const UUID &uu, const json &j, const std::filesystem::path &containing_dir);
    static constexpr Type s_type = Type::STL;
    json serialize() const override;

    double get_param(unsigned int point, unsigned int axis) const override;
    void set_param(unsigned int point, unsigned int axis, double value) override;

    glm::dvec3 get_point(unsigned int point, const Document &doc) const override;
    bool is_valid_point(unsigned int point) const override;

    glm::dvec3 m_origin = {0, 0, 0};
    glm::dquat m_normal;

    glm::dvec3 transform(glm::dvec3 p) const;

    std::filesystem::path m_path;
    std::filesystem::path get_path(const std::filesystem::path &containing_dir) const;

    // Kept for future parity with EntitySTEP (anchor points on the imported
    // mesh), even though no tool creates them yet -- see
    // tool_add_anchor.cpp/tool_move_anchor.cpp, which are still hardcoded to
    // EntitySTEP and explicitly out of scope for this pass.
    std::map<unsigned int, glm::dvec3> m_anchors;
    std::map<unsigned int, glm::dvec3> m_anchors_transformed;

    void add_anchor(unsigned int i, const glm::dvec3 &pt);
    void update_anchor(unsigned int i, const glm::dvec3 &pt);
    void remove_anchor(unsigned int i);

    bool delete_point(unsigned int point) override;

    std::shared_ptr<const ImportedSTL> m_imported;

    void update_imported(const std::filesystem::path &containing_dir);

    void move(const Entity &last, const glm::dvec3 &delta, unsigned int point) override;

    std::string get_point_name(unsigned int point) const override;

    void set_normal(const glm::dquat &q) override
    {
        m_normal = q;
    }
    glm::dquat get_normal() const override
    {
        return m_normal;
    }
};

} // namespace dune3d
