#include "mesh_decimate.hpp"
#include <map>
#include <set>
#include <queue>
#include <tuple>
#include <cmath>
#include <algorithm>
#include <iterator>

namespace dune3d {

namespace {

struct Vertex {
    double x, y, z;
};

double dist2(const Vertex &a, const Vertex &b)
{
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

double triangle_area(const Vertex &a, const Vertex &b, const Vertex &c)
{
    const double abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
    const double acx = c.x - a.x, acy = c.y - a.y, acz = c.z - a.z;
    const double cx = aby * acz - abz * acy;
    const double cy = abz * acx - abx * acz;
    const double cz = abx * acy - aby * acx;
    return 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
}

struct Face {
    int a, b, c;
    bool dead = false;
};

std::pair<int, int> edge_key(int a, int b)
{
    return a < b ? std::make_pair(a, b) : std::make_pair(b, a);
}

} // namespace

std::vector<MeshTriangle> decimate_mesh(const std::vector<MeshTriangle> &triangles, unsigned int target_face_count)
{
    // Recover a shared-vertex mesh from the (index-less) triangle soup by
    // exact-coordinate dedup -- transforming the same source vertex twice
    // always yields the identical double, so this is safe.
    std::map<std::tuple<double, double, double>, int> vertex_index;
    std::vector<Vertex> verts;
    auto get_index = [&](const gp_Pnt &p) {
        auto key = std::make_tuple(p.X(), p.Y(), p.Z());
        auto it = vertex_index.find(key);
        if (it != vertex_index.end())
            return it->second;
        const int idx = (int)verts.size();
        verts.push_back({p.X(), p.Y(), p.Z()});
        vertex_index.emplace(key, idx);
        return idx;
    };

    std::vector<Face> faces;
    faces.reserve(triangles.size());
    for (const auto &t : triangles)
        faces.push_back({get_index(t.a), get_index(t.b), get_index(t.c), false});

    std::vector<bool> dead_vert(verts.size(), false);
    std::vector<std::set<int>> vert_faces(verts.size());
    for (int fi = 0; fi < (int)faces.size(); fi++) {
        vert_faces[faces[fi].a].insert(fi);
        vert_faces[faces[fi].b].insert(fi);
        vert_faces[faces[fi].c].insert(fi);
    }

    // Kept up to date throughout (unlike a one-shot dedup set) so a
    // collapse's link-condition check below can look up which faces
    // currently sit on a given edge.
    std::map<std::pair<int, int>, std::vector<int>> edge_faces;
    for (int fi = 0; fi < (int)faces.size(); fi++) {
        const auto &f = faces[fi];
        edge_faces[edge_key(f.a, f.b)].push_back(fi);
        edge_faces[edge_key(f.b, f.c)].push_back(fi);
        edge_faces[edge_key(f.a, f.c)].push_back(fi);
    }

    struct QueueEntry {
        double len2;
        int a, b;
    };
    struct Cmp {
        bool operator()(const QueueEntry &x, const QueueEntry &y) const
        {
            return x.len2 > y.len2;
        }
    };
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, Cmp> pq;
    for (const auto &kv : edge_faces)
        pq.push({dist2(verts[kv.first.first], verts[kv.first.second]), kv.first.first, kv.first.second});

    int live_faces = (int)faces.size();
    while (!pq.empty() && live_faces > (int)target_face_count) {
        const auto e = pq.top();
        pq.pop();
        const int a = e.a, b = e.b;
        if (dead_vert[a] || dead_vert[b])
            continue;

        const auto it = edge_faces.find(edge_key(a, b));
        if (it == edge_faces.end())
            continue;
        std::vector<int> edge_live_faces;
        for (int fi : it->second)
            if (!faces[fi].dead)
                edge_live_faces.push_back(fi);
        if (edge_live_faces.empty())
            continue;

        // Link condition: collapsing (a,b) is manifold-safe only if the
        // common neighbors of a and b are exactly the opposite vertices of
        // the (at most 2) faces on this edge -- any extra common neighbor
        // means the collapse would weld two unrelated parts of the surface
        // together, tearing a hole where they used to be separate. Skipping
        // unsafe collapses (rather than doing them anyway) is what fixed a
        // real mesh going from 15 disconnected sewn shells down to 1 -- see
        // the plan for the before/after.
        std::set<int> opposite_verts;
        for (int fi : edge_live_faces) {
            const auto &f = faces[fi];
            const int opp = (f.a != a && f.a != b) ? f.a : (f.b != a && f.b != b) ? f.b : f.c;
            opposite_verts.insert(opp);
        }
        std::set<int> neigh_a, neigh_b;
        for (int fi : vert_faces[a]) {
            if (faces[fi].dead)
                continue;
            const auto &f = faces[fi];
            if (f.a != a)
                neigh_a.insert(f.a);
            if (f.b != a)
                neigh_a.insert(f.b);
            if (f.c != a)
                neigh_a.insert(f.c);
        }
        for (int fi : vert_faces[b]) {
            if (faces[fi].dead)
                continue;
            const auto &f = faces[fi];
            if (f.a != b)
                neigh_b.insert(f.a);
            if (f.b != b)
                neigh_b.insert(f.b);
            if (f.c != b)
                neigh_b.insert(f.c);
        }
        neigh_a.erase(b);
        neigh_b.erase(a);
        std::set<int> common;
        std::set_intersection(neigh_a.begin(), neigh_a.end(), neigh_b.begin(), neigh_b.end(),
                              std::inserter(common, common.begin()));
        if (common != opposite_verts)
            continue; // unsafe -- leave this edge uncollapsed

        // Merge b into a at the midpoint.
        verts[a] = {(verts[a].x + verts[b].x) / 2.0, (verts[a].y + verts[b].y) / 2.0,
                    (verts[a].z + verts[b].z) / 2.0};
        dead_vert[b] = true;

        const auto affected = vert_faces[b];
        for (int fi : affected) {
            if (faces[fi].dead)
                continue;
            Face &f = faces[fi];
            if (f.a == b)
                f.a = a;
            if (f.b == b)
                f.b = a;
            if (f.c == b)
                f.c = a;
            if (f.a == f.b || f.b == f.c || f.a == f.c) {
                f.dead = true;
                live_faces--;
            }
            else {
                vert_faces[a].insert(fi);
            }
        }
        vert_faces[b].clear();

        std::set<int> neigh;
        for (int fi : vert_faces[a]) {
            if (faces[fi].dead)
                continue;
            const Face &f = faces[fi];
            if (f.a != a)
                neigh.insert(f.a);
            if (f.b != a)
                neigh.insert(f.b);
            if (f.c != a)
                neigh.insert(f.c);
        }
        for (int n : neigh)
            pq.push({dist2(verts[a], verts[n]), a, n});
    }

    // Distinct-but-nearly-coincident vertex indices can still end up
    // extremely close together in actual position after repeated collapses
    // (a valid-by-topology but geometrically-degenerate sliver) -- confirmed
    // these can make BRepBuilderAPI_Sewing perform pathologically on the
    // resulting mesh, so they're filtered out here rather than left for the
    // sewing step to choke on.
    constexpr double s_min_triangle_area = 1e-4;

    std::vector<MeshTriangle> out;
    out.reserve(live_faces);
    for (const auto &f : faces) {
        if (f.dead)
            continue;
        const auto &va = verts[f.a];
        const auto &vb = verts[f.b];
        const auto &vc = verts[f.c];
        if (triangle_area(va, vb, vc) < s_min_triangle_area)
            continue;
        out.push_back({gp_Pnt(va.x, va.y, va.z), gp_Pnt(vb.x, vb.y, vb.z), gp_Pnt(vc.x, vc.y, vc.z)});
    }
    return out;
}

} // namespace dune3d
