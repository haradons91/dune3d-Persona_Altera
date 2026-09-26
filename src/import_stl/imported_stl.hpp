#pragma once
#include "import_stl.hpp"
#include <filesystem>
#include <atomic>

namespace dune3d {

class ImportedSTL {
public:
    ImportedSTL(const std::filesystem::path &p, const std::string &h) : path(p), hash(h)
    {
    }

    std::atomic_bool ready;
    const std::filesystem::path path;
    const std::string hash;
    STLImporter::Result result;
};
} // namespace dune3d
