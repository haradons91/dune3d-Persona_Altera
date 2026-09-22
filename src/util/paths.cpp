#include "paths.hpp"
#include <stdexcept>
#include "document/entity/ientity_in_workplane.hpp"
#include "document/entity/entity.hpp"
#include "document/entity/entity_circle2d.hpp"
#include "document/entity/entity_line2d.hpp"
#include "document/entity/entity_cluster.hpp"
#include "document/entity/entity_text.hpp"
#include "document/document.hpp"
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <limits>

namespace dune3d::paths {

Node::Node(const glm::dvec2 &ap) : p(ap)
{
}
bool Node::is_valid() const
{
    return connected_edges.size() == 2;
}
unsigned int Node::get_pt_for_edge(const Edge &edge) const
{
    for (const auto &[e, pt] : connected_edges) {
        if (e == &edge)
            return pt;
    }
    throw std::runtime_error("not an edge of node");
}

static Node &get_or_create_node(std::list<Node> &nodes, const glm::dvec2 &p)
{
    for (auto &node : nodes) {
        if (glm::length(node.p - p) < 1e-6)
            return node;
    }
    return nodes.emplace_back(p);
}


glm::dvec2 Paths::get_pt(const Entity &e, unsigned int pt, Edge::Transform tr)
{
    auto &en_wrkpl = dynamic_cast<const IEntityInWorkplane &>(e);
    auto p = en_wrkpl.get_point_in_workplane(pt);
    if (tr)
        return tr(p);
    else
        return p;
}

Edge::Edge(std::list<Node> &nodes, const Entity &e, Transform tr, std::optional<glm::dvec2> p1,
           std::optional<glm::dvec2> p2)
    : from(get_or_create_node(nodes, p1.value_or(Paths::get_pt(e, 1, tr)))),
      to(get_or_create_node(nodes, p2.value_or(Paths::get_pt(e, 2, tr)))), entity(e), transform_fn(tr), m_p1(p1), m_p2(p2)
{
    from.connected_edges.emplace(this, 1);
    to.connected_edges.emplace(this, 2);
}

glm::dvec2 Edge::get_point(unsigned int pt) const
{
    if (pt == 1 && m_p1)
        return *m_p1;
    if (pt == 2 && m_p2)
        return *m_p2;
    return Paths::get_pt(entity, pt, transform_fn);
}

Edge::Edge(Node &node, const EntityCircle2D &e, Transform tr) : from(node), to(node), entity(e), transform_fn(tr)
{
}

Node &Edge::get_other_node(Node &node)
{
    if (&node == &from)
        return to;
    else if (&node == &to)
        return from;
    assert(false);
}

glm::dvec2 Edge::transform(const glm::dvec2 &v) const
{
    if (transform_fn)
        return transform_fn(v);
    else
        return v;
}

std::array<Edge *, 2> Node::get_edges()
{
    assert(connected_edges.size() == 2);
    auto it = connected_edges.begin();
    auto &e1 = *it++;
    auto &e2 = *it;
    if (e1.first->entity.m_uuid < e2.first->entity.m_uuid)
        return {e1.first, e2.first};
    else
        return {e2.first, e1.first};
}


static bool entity_is_valid(const Entity &en, Edge::Transform tr)
{
    if (auto en_line = dynamic_cast<const EntityLine2D *>(&en))
        if (glm::length(tr(en_line->m_p1) - tr(en_line->m_p2)) < 1e-6)
            return false;
    return true;
}

static double path_signed_area(const Path &path)
{
    if (path.size() == 1) {
        if (const auto *circle = dynamic_cast<const EntityCircle2D *>(&path.front().second.entity))
            return M_PI * circle->m_radius * circle->m_radius;
    }
    double area = 0;
    for (size_t i = 0; i < path.size(); i++) {
        const auto &a = path.at(i).first.p;
        const auto &b = path.at((i + 1) % path.size()).first.p;
        area += a.x * b.y - b.x * a.y;
    }
    return area / 2.;
}

static double path_area(const Path &path)
{
    return std::abs(path_signed_area(path));
}

static glm::dvec2 path_sample(const Path &path)
{
    if (path.size() == 1) {
        if (const auto *circle = dynamic_cast<const EntityCircle2D *>(&path.front().second.entity))
            return circle->m_center;
    }
    glm::dvec2 sample{0, 0};
    for (const auto &[node, edge] : path)
        sample += node.p;
    return sample / static_cast<double>(path.size());
}

static bool point_in_path(const glm::dvec2 &point, const Path &path)
{
    if (path.size() == 1) {
        if (const auto *circle = dynamic_cast<const EntityCircle2D *>(&path.front().second.entity))
            return glm::length(point - circle->m_center) < circle->m_radius;
        return false;
    }
    bool inside = false;
    for (size_t i = 0, j = path.size() - 1; i < path.size(); j = i++) {
        const auto &a = path.at(i).first.p;
        const auto &b = path.at(j).first.p;
        if ((a.y > point.y) != (b.y > point.y)
            && point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}

Paths Paths::from_document(const Document &doc, const UUID &wrkpl_uu, const UUID &source_group_uu)
{
    Paths paths;
    for (const auto &[uu, en] : doc.m_entities) {
        if (en->m_group != source_group_uu)
            continue;
        if (en->m_construction)
            continue;
        if (en->get_type() == Entity::Type::CIRCLE_2D)
            continue;
        if (en->get_type() == Entity::Type::POINT_2D)
            continue;
        if (auto en_wrkpl = dynamic_cast<const IEntityInWorkplane *>(en.get())) {
            if (en_wrkpl->get_workplane() != wrkpl_uu)
                continue;
            if (!entity_is_valid(*en, [](const auto &x) { return x; }))
                continue;
            if (auto en_cluster = dynamic_cast<const EntityCluster *>(en.get())) {
                auto tr = [en_cluster](const glm::dvec2 &v) { return en_cluster->transform(v); };
                for (const auto &[uu2, en2] : en_cluster->m_content->m_entities) {
                    if (en2->m_construction)
                        continue;
                    if (!entity_is_valid(*en2, tr))
                        continue;
                    if (en2->of_type(Entity::Type::CIRCLE_2D))
                        continue;

                    paths.edges.emplace_back(paths.nodes, *en2, tr);
                }
            }
            else if (auto en_text = dynamic_cast<const EntityText *>(en.get())) {
                auto tr = [en_text](const glm::dvec2 &v) { return en_text->transform(v); };
                for (const auto &[uu2, en2] : en_text->m_content->m_entities) {
                    if (!entity_is_valid(*en2, tr))
                        continue;
                    if (en2->of_type(Entity::Type::CIRCLE_2D))
                        continue;

                    paths.edges.emplace_back(paths.nodes, *en2, tr);
                }
            }
            else {
                paths.edges.emplace_back(paths.nodes, *en, nullptr);
            }
        }
    }

    struct LineData {
        Edge *edge;
        glm::dvec2 a;
        glm::dvec2 b;
    };
    std::vector<LineData> lines;
    for (auto &edge : paths.edges) {
        if (edge.entity.of_type(Entity::Type::LINE_2D))
            lines.push_back({&edge, edge.get_point(1), edge.get_point(2)});
    }

    auto cross2 = [](const glm::dvec2 &a, const glm::dvec2 &b) { return a.x * b.y - a.y * b.x; };
    std::map<Edge *, std::vector<glm::dvec2>> split_points;
    for (size_t i = 0; i < lines.size(); i++) {
        for (size_t j = i + 1; j < lines.size(); j++) {
            const auto r = lines[i].b - lines[i].a;
            const auto s = lines[j].b - lines[j].a;
            const auto denominator = cross2(r, s);
            if (std::abs(denominator) < 1e-9)
                continue;
            const auto delta = lines[j].a - lines[i].a;
            const auto t = cross2(delta, s) / denominator;
            const auto u = cross2(delta, r) / denominator;
            const bool t_in_segment = t > -1e-6 && t < 1. + 1e-6;
            const bool u_in_segment = u > -1e-6 && u < 1. + 1e-6;
            const bool t_interior = t > 1e-6 && t < 1. - 1e-6;
            const bool u_interior = u > 1e-6 && u < 1. - 1e-6;
            // Also split when an endpoint of one line lands in the interior
            // of another line, which is how the corner triangle connects to
            // the rectangle in the sketch.
            if (t_in_segment && u_in_segment && (t_interior || u_interior)) {
                const auto point = lines[i].a + r * t;
                if (t_interior)
                    split_points[lines[i].edge].push_back(point);
                if (u_interior)
                    split_points[lines[j].edge].push_back(point);
            }
        }
    }

    for (auto &[edge, points] : split_points) {
        const auto a = edge->get_point(1);
        const auto b = edge->get_point(2);
        points.push_back(a);
        points.push_back(b);
        std::ranges::sort(points, [a, b](const auto &p1, const auto &p2) {
            return glm::length(p1 - a) < glm::length(p2 - a);
        });
        edge->from.connected_edges.erase({edge, 1});
        edge->to.connected_edges.erase({edge, 2});
        const auto &entity = edge->entity;
        const auto transform = edge->transform_fn;
        for (size_t i = 1; i < points.size(); i++)
            paths.edges.emplace_back(paths.nodes, entity, transform, points[i - 1], points[i]);
        auto it = std::ranges::find_if(paths.edges, [edge](const auto &candidate) { return &candidate == edge; });
        paths.edges.erase(it);
    }

    // Find closed cycles in the sketch graph.  A node may have more than two
    // edges when another line branches from a rectangle, so requiring exactly
    // two edges loses the surrounding profiles (and makes the whole sketch
    // appear to be the only selectable object).
    std::map<Node *, size_t> node_indices;
    size_t node_index = 0;
    for (auto &node : paths.nodes)
        node_indices.emplace(&node, node_index++);

    std::set<std::set<Edge *>> found_cycles;
    for (auto &start_node : paths.nodes) {
        const auto start = &start_node;
        const auto start_index = node_indices.at(start);
        std::set<Node *> visited_nodes{start};
        std::set<Edge *> visited_edges;
        Path path;

        std::function<void(Node *)> find_cycles = [&](Node *node) {
            for (const auto &[edge, point] : node->connected_edges) {
                (void)point;
                if (visited_edges.contains(edge))
                    continue;
                auto &next = edge->get_other_node(*node);
                if (node_indices.at(&next) < start_index)
                    continue;

                if (&next == start) {
                    if (path.size() >= 2) {
                        auto cycle_edges = visited_edges;
                        cycle_edges.insert(edge);
                        if (found_cycles.insert(cycle_edges).second) {
                            auto closed_path = path;
                            closed_path.emplace_back(*node, *edge);
                            paths.paths.emplace_back(std::move(closed_path));
                        }
                    }
                    continue;
                }
                if (visited_nodes.contains(&next))
                    continue;

                visited_nodes.insert(&next);
                visited_edges.insert(edge);
                path.emplace_back(*node, *edge);
                find_cycles(&next);
                path.pop_back();
                visited_edges.erase(edge);
                visited_nodes.erase(&next);
            }
        };
        find_cycles(start);
    }

    // add circles
    for (const auto &[uu, en] : doc.m_entities) {
        if (en->m_group != source_group_uu)
            continue;
        if (en->m_construction)
            continue;
        if (en->of_type(Entity::Type::CIRCLE_2D)) {
            auto &circle = dynamic_cast<const EntityCircle2D &>(*en);
            if (circle.get_workplane() != wrkpl_uu)
                continue;

            auto &node = paths.nodes.emplace_back(circle.m_center);
            auto &edge = paths.edges.emplace_back(node, circle, nullptr);
            Path path;
            path.emplace_back(node, edge);
            paths.paths.emplace_back(std::move(path));
        }
        else if (auto en_cluster = dynamic_cast<const EntityCluster *>(en.get())) {
            auto tr = [en_cluster](const glm::dvec2 &v) { return en_cluster->transform(v); };
            for (const auto &[uu2, en2] : en_cluster->m_content->m_entities) {
                if (en2->m_construction)
                    continue;
                if (en2->of_type(Entity::Type::CIRCLE_2D)) {
                    auto &circle = dynamic_cast<const EntityCircle2D &>(*en2);
                    auto &node = paths.nodes.emplace_back(en_cluster->transform(circle.m_center));
                    auto &edge = paths.edges.emplace_back(node, circle, tr);
                    Path path;
                    path.emplace_back(node, edge);
                    paths.paths.emplace_back(std::move(path));
                }
            }
        }
    }

    // Cycle discovery walks pointer-based adjacency sets.  Pointer ordering
    // can differ between calls, which previously caused the same geometry to
    // receive different profile indices in the renderer and extruder.  Sort
    // paths by the UUIDs of their source entities so profile indices are
    // stable across every Paths::from_document call.
    // Normalize winding before assigning profile indices.  The cycle walk
    // can discover the same loop in either direction, which changes how the
    // downstream contour boolean interprets touching inner profiles.
    for (auto &path : paths.paths) {
        if (path.size() < 2 || path_signed_area(path) >= 0)
            continue;
        Path reversed;
        for (size_t i = 0; i < path.size(); i++) {
            const auto node_index = path.size() - 1 - i;
            const auto edge_index = (node_index + path.size() - 1) % path.size();
            reversed.emplace_back(path.at(node_index).first, path.at(edge_index).second);
        }
        path = std::move(reversed);
    }

    const auto path_key = [](const Path &path) {
        std::vector<std::string> entities;
        entities.reserve(path.size());
        for (const auto &[node, edge] : path)
            entities.emplace_back(static_cast<std::string>(edge.entity.m_uuid));
        std::ranges::sort(entities);
        std::string key;
        for (const auto &entity : entities)
            key += entity + ";";
        return key;
    };
    std::ranges::stable_sort(paths.paths, [&path_key](const Path &a, const Path &b) {
        const auto area_a = path_area(a);
        const auto area_b = path_area(b);
        if (std::abs(area_a - area_b) > 1e-9)
            return area_a > area_b;
        return path_key(a) < path_key(b);
    });

    // Intersections can make the cycle walk report the same outer boundary
    // twice, once in each orientation, with slightly different edge sets.
    // Such duplicate contours make the downstream even-odd boolean invalid.
    for (size_t i = 0; i < paths.paths.size(); i++) {
        const auto bounds = [](const Path &path) {
            glm::dvec2 minimum{std::numeric_limits<double>::max()};
            glm::dvec2 maximum{std::numeric_limits<double>::lowest()};
            for (const auto &[node, edge] : path) {
                minimum = glm::min(minimum, node.p);
                maximum = glm::max(maximum, node.p);
            }
            return std::pair{minimum, maximum};
        };
        const auto [min_i, max_i] = bounds(paths.paths.at(i));
        for (size_t j = paths.paths.size(); j-- > i + 1;) {
            const auto [min_j, max_j] = bounds(paths.paths.at(j));
            const auto area_i = path_area(paths.paths.at(i));
            const auto area_j = path_area(paths.paths.at(j));
            // Coincident loops can differ slightly after constraint solving.
            // Treat those as duplicates before building cells; retaining two
            // identical holes makes the even-odd fill rule cancel them and
            // incorrectly fills the nested profile.
            const auto same_bounds = glm::length(min_i - min_j) < 1e-4 && glm::length(max_i - max_j) < 1e-4;
            const auto similar_area = std::min(area_i, area_j) > 1e-6
                                     && std::max(area_i, area_j) / std::min(area_i, area_j) < 1.05;
            if (same_bounds && similar_area)
                paths.paths.erase(paths.paths.begin() + static_cast<std::ptrdiff_t>(j));
        }
    }

    // Convert boundary loops into planar face cells.  A cell is represented
    // by its outer boundary and every smaller loop contained by that
    // boundary.  The solid builder's even-odd fill rule then turns those
    // enclosed loops into holes.
    for (unsigned int boundary = 0; boundary < paths.paths.size(); boundary++) {
        const auto boundary_area = path_area(paths.paths.at(boundary));
        Cell cell{.boundary = boundary};
        for (unsigned int other = 0; other < paths.paths.size(); other++) {
            if (other == boundary || path_area(paths.paths.at(other)) >= boundary_area)
                continue;
            if (point_in_path(path_sample(paths.paths.at(other)), paths.paths.at(boundary)))
                cell.holes.insert(other);
        }
        paths.cells.emplace_back(std::move(cell));
    }
    return paths;
}


} // namespace dune3d::paths
