#pragma once
#include "import_3mf.hpp"
#include <filesystem>
#include <atomic>

namespace dune3d {

class Imported3MF {
public:
    Imported3MF(const std::filesystem::path &p, const std::string &h) : path(p), hash(h)
    {
    }

    std::atomic_bool ready;
    const std::filesystem::path path;
    const std::string hash;
    ThreeMFImporter::Result result;
};
} // namespace dune3d
