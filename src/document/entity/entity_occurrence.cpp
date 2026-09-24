#include "entity_occurrence.hpp"
#include "nlohmann/json.hpp"
#include "util/glm_util.hpp"
#include "util/json_util.hpp"
#include "entityt_impl.hpp"

namespace dune3d {
EntityOccurrence::EntityOccurrence(const UUID &uu) : Base(uu), m_normal(glm::quat_identity<double, glm::defaultp>())
{
}

EntityOccurrence::EntityOccurrence(const UUID &uu, const json &j)
    : Base(uu, j), m_component(j.at("component").get<UUID>()), m_origin(j.at("origin").get<glm::dvec3>()),
      m_normal(j.at("normal").get<glm::dquat>()), m_instance_label(j.value("instance_label", ""))
{
}

json EntityOccurrence::serialize() const
{
    json j = Entity::serialize();
    j["component"] = m_component;
    j["origin"] = m_origin;
    j["normal"] = m_normal;
    j["instance_label"] = m_instance_label;
    return j;
}

glm::dvec3 EntityOccurrence::transform(glm::dvec3 p) const
{
    return glm::rotate(m_normal, p) + m_origin;
}

double EntityOccurrence::get_param(unsigned int point, unsigned int axis) const
{
    if (point == 1)
        return m_origin[axis];
    else if (point == 2)
        return m_normal[axis];
    return NAN;
}

void EntityOccurrence::set_param(unsigned int point, unsigned int axis, double value)
{
    if (point == 1)
        m_origin[axis] = value;
    else if (point == 2)
        m_normal[axis] = value;
}

glm::dvec3 EntityOccurrence::get_point(unsigned int point, const Document &doc) const
{
    if (point == 1)
        return m_origin;
    return {NAN, NAN, NAN};
}

std::string EntityOccurrence::get_point_name(unsigned int point) const
{
    switch (point) {
    case 0:
        return "";
    case 1:
        return "origin";
    default:
        return "";
    }
}

bool EntityOccurrence::is_valid_point(unsigned int point) const
{
    return point == 1;
}

void EntityOccurrence::move(const Entity &last, const glm::dvec3 &delta, unsigned int point)
{
    auto &en_last = dynamic_cast<const EntityOccurrence &>(last);
    if (point == 0 || point == 1)
        m_origin = en_last.m_origin + delta;
}

} // namespace dune3d
