#include "mesh_convex_hull.hpp"
#include <map>
#include <set>
#include <tuple>
#include <cmath>
#include <algorithm>

namespace dune3d {

namespace {

struct V3 {
    double x, y, z;
    V3 operator-(const V3 &o) const
    {
        return {x - o.x, y - o.y, z - o.z};
    }
    V3 cross(const V3 &o) const
    {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    double dot(const V3 &o) const
    {
        return x * o.x + y * o.y + z * o.z;
    }
};

struct HullFace {
    int a, b, c;
    V3 normal;
};

V3 face_normal(const std::vector<V3> &pts, int a, int b, int c)
{
    return (pts[b] - pts[a]).cross(pts[c] - pts[a]);
}

double signed_dist(const std::vector<V3> &pts, const HullFace &f, int p)
{
    return f.normal.dot(pts[p] - pts[f.a]);
}

// Incremental convex hull is O(n * current_hull_face_count) -- fine for
// typical meshes, but round/cylindrical shapes can leave thousands of points
// near the true hull surface for most of the run, making that blow up (
// confirmed hanging on a real bearing STL with ~12k points). Pre-reducing to
// a representative subset via grid bucketing keeps every algorithm's actual
// bottleneck (visibility tests against the growing face list) bounded,
// while barely changing the resulting hull -- a hull is defined by a mesh's
// extremal points, and those overwhelmingly survive a reasonably fine grid
// bucketing.
constexpr size_t s_max_hull_points = 3000;

std::vector<V3> reduce_points(const std::vector<V3> &pts)
{
    if (pts.size() <= s_max_hull_points)
        return pts;

    double xmin = 1e30, ymin = 1e30, zmin = 1e30, xmax = -1e30, ymax = -1e30, zmax = -1e30;
    for (const auto &p : pts) {
        xmin = std::min(xmin, p.x);
        ymin = std::min(ymin, p.y);
        zmin = std::min(zmin, p.z);
        xmax = std::max(xmax, p.x);
        ymax = std::max(ymax, p.y);
        zmax = std::max(zmax, p.z);
    }
    const double diag =
            std::sqrt((xmax - xmin) * (xmax - xmin) + (ymax - ymin) * (ymax - ymin) + (zmax - zmin) * (zmax - zmin));
    double cell = diag / std::cbrt((double)s_max_hull_points) * 0.5;
    if (cell <= 0)
        cell = 1e-6;

    std::set<std::tuple<long, long, long>> seen;
    std::vector<V3> reduced;
    for (const auto &p : pts) {
        auto key = std::make_tuple((long)std::floor(p.x / cell), (long)std::floor(p.y / cell),
                                   (long)std::floor(p.z / cell));
        if (seen.insert(key).second)
            reduced.push_back(p);
    }
    return reduced;
}

} // namespace

std::vector<MeshTriangle> convex_hull(const std::vector<MeshTriangle> &triangles)
{
    std::map<std::tuple<double, double, double>, int> vertex_index;
    std::vector<V3> all_pts;
    auto get_index = [&](const gp_Pnt &p) {
        auto key = std::make_tuple(p.X(), p.Y(), p.Z());
        auto it = vertex_index.find(key);
        if (it != vertex_index.end())
            return it->second;
        const int idx = (int)all_pts.size();
        all_pts.push_back({p.X(), p.Y(), p.Z()});
        vertex_index.emplace(key, idx);
        return idx;
    };
    for (const auto &t : triangles) {
        get_index(t.a);
        get_index(t.b);
        get_index(t.c);
    }

    const auto pts = reduce_points(all_pts);
    if (pts.size() < 4)
        return {};

    int i0 = 0, i1 = 0;
    for (size_t i = 1; i < pts.size(); i++) {
        if (pts[i].x < pts[i0].x)
            i0 = (int)i;
        if (pts[i].x > pts[i1].x)
            i1 = (int)i;
    }
    if (i0 == i1)
        return {}; // degenerate: all points coincide in x

    const V3 dir = pts[i1] - pts[i0];
    int i2 = -1;
    double best = -1;
    for (size_t i = 0; i < pts.size(); i++) {
        if ((int)i == i0 || (int)i == i1)
            continue;
        const V3 v = pts[i] - pts[i0];
        const V3 c = v.cross(dir);
        const double d = c.dot(c);
        if (d > best) {
            best = d;
            i2 = (int)i;
        }
    }
    if (i2 < 0)
        return {}; // degenerate: all points collinear

    const V3 n012 = face_normal(pts, i0, i1, i2);
    int i3 = -1;
    best = -1;
    for (size_t i = 0; i < pts.size(); i++) {
        if ((int)i == i0 || (int)i == i1 || (int)i == i2)
            continue;
        const double d = std::fabs(n012.dot(pts[i] - pts[i0]));
        if (d > best) {
            best = d;
            i3 = (int)i;
        }
    }
    if (i3 < 0 || best < 1e-12)
        return {}; // degenerate: all points coplanar, no volume

    std::vector<HullFace> faces;
    auto add_face_oriented = [&](int a, int b, int c, int opposite_pt) {
        HullFace f{a, b, c, face_normal(pts, a, b, c)};
        if (signed_dist(pts, f, opposite_pt) > 0) {
            std::swap(f.b, f.c);
            f.normal = face_normal(pts, f.a, f.b, f.c);
        }
        faces.push_back(f);
    };
    add_face_oriented(i0, i1, i2, i3);
    add_face_oriented(i0, i1, i3, i2);
    add_face_oriented(i0, i2, i3, i1);
    add_face_oriented(i1, i2, i3, i0);

    std::vector<bool> used(pts.size(), false);
    used[i0] = used[i1] = used[i2] = used[i3] = true;

    constexpr double eps = 1e-9;
    for (size_t pi = 0; pi < pts.size(); pi++) {
        if (used[pi])
            continue;

        std::vector<int> visible;
        for (int fi = 0; fi < (int)faces.size(); fi++) {
            if (signed_dist(pts, faces[fi], (int)pi) > eps)
                visible.push_back(fi);
        }
        if (visible.empty())
            continue; // inside the current hull

        // A directed edge of a visible face is on the horizon iff its
        // reverse isn't also a directed edge of some visible face.
        std::set<std::pair<int, int>> visible_edges;
        for (int fi : visible) {
            const auto &f = faces[fi];
            visible_edges.insert({f.a, f.b});
            visible_edges.insert({f.b, f.c});
            visible_edges.insert({f.c, f.a});
        }
        std::vector<std::pair<int, int>> horizon;
        for (int fi : visible) {
            const auto &f = faces[fi];
            const int e[3][2] = {{f.a, f.b}, {f.b, f.c}, {f.c, f.a}};
            for (const auto &ed : e) {
                if (!visible_edges.count({ed[1], ed[0]}))
                    horizon.push_back({ed[0], ed[1]});
            }
        }

        std::set<int> visible_set(visible.begin(), visible.end());
        std::vector<HullFace> kept;
        kept.reserve(faces.size() - visible.size());
        for (int fi = 0; fi < (int)faces.size(); fi++) {
            if (!visible_set.count(fi))
                kept.push_back(faces[fi]);
        }
        faces = std::move(kept);

        for (const auto &ed : horizon)
            faces.push_back({ed.first, ed.second, (int)pi, face_normal(pts, ed.first, ed.second, (int)pi)});

        used[pi] = true;
    }

    std::vector<MeshTriangle> out;
    out.reserve(faces.size());
    for (const auto &f : faces) {
        out.push_back({gp_Pnt(pts[f.a].x, pts[f.a].y, pts[f.a].z), gp_Pnt(pts[f.b].x, pts[f.b].y, pts[f.b].z),
                       gp_Pnt(pts[f.c].x, pts[f.c].y, pts[f.c].z)});
    }
    return out;
}

} // namespace dune3d
