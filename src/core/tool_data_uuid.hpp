#pragma once
#include "tool_data.hpp"
#include "util/uuid.hpp"

namespace dune3d {
class ToolDataUUID : public ToolData {
public:
    ToolDataUUID(const UUID &u) : uuid(u)
    {
    }
    ToolDataUUID()
    {
    }
    UUID uuid;
};
} // namespace dune3d
