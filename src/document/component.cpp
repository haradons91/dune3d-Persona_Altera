#include "component.hpp"
#include "nlohmann/json.hpp"

namespace dune3d {

Component::Component(const UUID &uu) : m_uuid(uu)
{
}

Component::Component(const UUID &uu, const json &j, const std::filesystem::path &containing_dir)
    : m_uuid(uu), m_name(j.at("name").get<std::string>()), m_document(j.at("document"), containing_dir)
{
}

std::unique_ptr<Component> Component::clone() const
{
    return std::make_unique<Component>(*this);
}

json Component::serialize() const
{
    return json{{"name", m_name}, {"document", m_document.serialize()}};
}

} // namespace dune3d
