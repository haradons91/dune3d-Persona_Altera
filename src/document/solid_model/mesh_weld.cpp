#include "mesh_weld.hpp"
#include <map>
#include <tuple>
#include <cmath>

namespace dune3d {

std::vector<MeshTriangle> weld_mesh(const std::vector<MeshTriangle> &triangles, double tolerance)
{
    if (tolerance <= 0)
        return triangles;

    std::map<std::tuple<long, long, long>, gp_Pnt> buckets;
    auto weld = [&](const gp_Pnt &p) {
        auto key = std::make_tuple((long)std::floor(p.X() / tolerance), (long)std::floor(p.Y() / tolerance),
                                   (long)std::floor(p.Z() / tolerance));
        auto it = buckets.find(key);
        if (it != buckets.end())
            return it->second;
        buckets.emplace(key, p);
        return p;
    };

    std::vector<MeshTriangle> out;
    out.reserve(triangles.size());
    for (const auto &t : triangles) {
        MeshTriangle w{weld(t.a), weld(t.b), weld(t.c)};
        if (w.a.IsEqual(w.b, 0) || w.b.IsEqual(w.c, 0) || w.a.IsEqual(w.c, 0))
            continue;
        out.push_back(w);
    }
    return out;
}

} // namespace dune3d
