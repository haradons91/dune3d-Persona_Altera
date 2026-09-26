#pragma once
#include <memory>
#include <map>
#include "imported_3mf.hpp"

namespace dune3d {

class ThreeMFImportManager {
public:
    static ThreeMFImportManager &get();
    std::shared_ptr<Imported3MF> import_3mf(const std::filesystem::path &path);

private:
    ThreeMFImportManager();
    std::map<std::filesystem::path, std::shared_ptr<Imported3MF>> m_imported;
};

} // namespace dune3d
