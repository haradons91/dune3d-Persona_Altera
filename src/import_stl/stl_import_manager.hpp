#pragma once
#include <memory>
#include <map>
#include "imported_stl.hpp"

namespace dune3d {

class STLImportManager {
public:
    static STLImportManager &get();
    std::shared_ptr<ImportedSTL> import_stl(const std::filesystem::path &path);

private:
    STLImportManager();
    std::map<std::filesystem::path, std::shared_ptr<ImportedSTL>> m_imported;
};

} // namespace dune3d
