#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>

namespace dune3d {

// Minimal ZIP central-directory reader for pulling a single named entry
// out of a .3mf archive. Handles the common case only (store/deflate, no
// zip64/multi-disk/encryption) -- covers every file mainstream slicers
// and CAD tools produce. Returns nullopt on anything it can't handle.
std::optional<std::vector<uint8_t>> read_zip_entry(const std::filesystem::path &zip_path,
                                                   const std::string &entry_name);

} // namespace dune3d
