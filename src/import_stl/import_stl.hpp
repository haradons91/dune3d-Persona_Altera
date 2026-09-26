#pragma once
#include "canvas/face.hpp"
#include <filesystem>

namespace dune3d::STLImporter {
using namespace dune3d::face;

// Reference-only: no points/edges/shapes like STEPImporter::Result -- STL
// has no BREP topology, just a triangle mesh, and this mesh never
// participates in solid-model operations (see the STL import plan).
class Result {
public:
    Faces faces;
};

Result import(const std::filesystem::path &filename);
} // namespace dune3d::STLImporter
