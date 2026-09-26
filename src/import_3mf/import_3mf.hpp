#pragma once
#include "canvas/face.hpp"
#include <filesystem>

namespace dune3d::ThreeMFImporter {
using namespace dune3d::face;

// Reference-only, same shape as STLImporter::Result -- see the 3MF import
// plan. All build items are flattened into one combined mesh.
class Result {
public:
    Faces faces;
};

Result import(const std::filesystem::path &filename);
} // namespace dune3d::ThreeMFImporter
