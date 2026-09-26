#include "import_3mf.hpp"
#include "zip_reader.hpp"
#include <glibmm/markup.h>
#include <glm/glm.hpp>
#include <map>
#include <set>
#include <array>
#include <limits>
#include <algorithm>
#include <cstdlib>

namespace dune3d::ThreeMFImporter {

namespace {

// Row-major 3x4 affine transform, matching the 3MF spec's "transform"
// attribute convention: a point (row vector) is transformed via
// p' = p*L + t, where L is the 3x3 linear part (rows m[0..2]) and t is
// the translation (m[3]).
struct Transform {
    double m[4][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}};
};

glm::dvec3 apply(const Transform &t, const glm::dvec3 &p)
{
    return {p.x * t.m[0][0] + p.y * t.m[1][0] + p.z * t.m[2][0] + t.m[3][0],
            p.x * t.m[0][1] + p.y * t.m[1][1] + p.z * t.m[2][1] + t.m[3][1],
            p.x * t.m[0][2] + p.y * t.m[1][2] + p.z * t.m[2][2] + t.m[3][2]};
}

// Composes two transforms so that a point is transformed by `a` first,
// then by `b`: p'' = (p*L_a + t_a)*L_b + t_b = p*(L_a*L_b) + (t_a*L_b + t_b).
Transform compose(const Transform &a, const Transform &b)
{
    Transform r;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            double s = 0;
            for (int k = 0; k < 3; k++)
                s += a.m[i][k] * b.m[k][j];
            r.m[i][j] = s;
        }
    }
    for (int j = 0; j < 3; j++) {
        double s = b.m[3][j];
        for (int k = 0; k < 3; k++)
            s += a.m[3][k] * b.m[k][j];
        r.m[3][j] = s;
    }
    return r;
}

Transform parse_transform(const std::string &s)
{
    Transform t;
    if (s.empty())
        return t;
    double v[12];
    int n = 0;
    const char *p = s.c_str();
    char *end;
    for (; n < 12; n++) {
        v[n] = std::strtod(p, &end);
        if (end == p)
            break;
        p = end;
    }
    if (n != 12)
        return Transform{};
    t.m[0][0] = v[0];
    t.m[0][1] = v[1];
    t.m[0][2] = v[2];
    t.m[1][0] = v[3];
    t.m[1][1] = v[4];
    t.m[1][2] = v[5];
    t.m[2][0] = v[6];
    t.m[2][1] = v[7];
    t.m[2][2] = v[8];
    t.m[3][0] = v[9];
    t.m[3][1] = v[10];
    t.m[3][2] = v[11];
    return t;
}

double unit_to_mm(const std::string &unit)
{
    if (unit == "micron")
        return 0.001;
    if (unit == "centimeter")
        return 10.;
    if (unit == "meter")
        return 1000.;
    if (unit == "inch")
        return 25.4;
    if (unit == "foot")
        return 304.8;
    return 1.; // millimeter, the 3MF default, and the fallback for anything unrecognized
}

struct ObjectMesh {
    std::vector<glm::dvec3> vertices;
    std::vector<std::array<int, 3>> triangles;
};

struct ObjectComponent {
    std::string objectid;
    Transform transform;
};

struct Object {
    bool is_mesh = false;
    ObjectMesh mesh;
    std::vector<ObjectComponent> components;
};

std::string get_attr(const Glib::Markup::Parser::AttributeMap &attrs, const char *name, const char *def = "")
{
    auto it = attrs.find(name);
    return it != attrs.end() ? it->second.raw() : std::string(def);
}

class ModelParser : public Glib::Markup::Parser {
public:
    std::map<std::string, Object> objects;
    std::vector<std::pair<std::string, Transform>> build_items;
    double unit_scale = 1.;

protected:
    void on_start_element(Glib::Markup::ParseContext &, const Glib::ustring &name,
                          const AttributeMap &attrs) override
    {
        if (name == "model") {
            unit_scale = unit_to_mm(get_attr(attrs, "unit", "millimeter"));
        }
        else if (name == "object") {
            m_current_object = get_attr(attrs, "id");
            objects[m_current_object];
            m_in_object = true;
        }
        else if (name == "mesh" && m_in_object) {
            objects[m_current_object].is_mesh = true;
        }
        else if (name == "vertex" && m_in_object) {
            objects[m_current_object].mesh.vertices.emplace_back(
                    std::strtod(get_attr(attrs, "x", "0").c_str(), nullptr),
                    std::strtod(get_attr(attrs, "y", "0").c_str(), nullptr),
                    std::strtod(get_attr(attrs, "z", "0").c_str(), nullptr));
        }
        else if (name == "triangle" && m_in_object) {
            objects[m_current_object].mesh.triangles.push_back(
                    {std::atoi(get_attr(attrs, "v1", "0").c_str()), std::atoi(get_attr(attrs, "v2", "0").c_str()),
                     std::atoi(get_attr(attrs, "v3", "0").c_str())});
        }
        else if (name == "component" && m_in_object) {
            objects[m_current_object].components.push_back(
                    {get_attr(attrs, "objectid"), parse_transform(get_attr(attrs, "transform"))});
        }
        else if (name == "item") {
            build_items.emplace_back(get_attr(attrs, "objectid"), parse_transform(get_attr(attrs, "transform")));
        }
    }

    void on_end_element(Glib::Markup::ParseContext &, const Glib::ustring &name) override
    {
        if (name == "object")
            m_in_object = false;
    }

private:
    std::string m_current_object;
    bool m_in_object = false;
};

void resolve_object_into(const std::map<std::string, Object> &objects, const std::string &id,
                         const Transform &transform, double unit_scale, Face &face_out,
                         std::vector<glm::dvec3> &normal_acc, std::set<std::string> &visiting)
{
    if (visiting.contains(id))
        return; // component cycle guard
    auto it = objects.find(id);
    if (it == objects.end())
        return;
    const auto &obj = it->second;
    if (obj.is_mesh) {
        const size_t base = face_out.vertices.size();
        for (const auto &v : obj.mesh.vertices) {
            const auto vt = apply(transform, v * unit_scale);
            face_out.vertices.emplace_back(vt.x, vt.y, vt.z);
        }
        normal_acc.resize(face_out.vertices.size(), {0, 0, 0});
        for (const auto &tri : obj.mesh.triangles) {
            const auto ia = base + tri[0];
            const auto ib = base + tri[1];
            const auto ic = base + tri[2];
            if (ia >= face_out.vertices.size() || ib >= face_out.vertices.size()
                || ic >= face_out.vertices.size())
                continue;
            face_out.triangle_indices.emplace_back(ia, ib, ic);
            const auto &va = face_out.vertices[ia];
            const auto &vb = face_out.vertices[ib];
            const auto &vc = face_out.vertices[ic];
            const glm::dvec3 pa(va.x, va.y, va.z);
            const glm::dvec3 pb(vb.x, vb.y, vb.z);
            const glm::dvec3 pc(vc.x, vc.y, vc.z);
            // Not normalized -- weights the accumulation by triangle area,
            // same effect as OpenCascade's own per-vertex normal averaging
            // that the STL/STEP importers rely on.
            const auto n = glm::cross(pb - pa, pc - pa);
            normal_acc[ia] += n;
            normal_acc[ib] += n;
            normal_acc[ic] += n;
        }
    }
    else {
        visiting.insert(id);
        for (const auto &comp : obj.components)
            resolve_object_into(objects, comp.objectid, compose(comp.transform, transform), unit_scale, face_out,
                                normal_acc, visiting);
        visiting.erase(id);
    }
}

} // namespace

Result import(const std::filesystem::path &filename)
{
    Result result;

    auto data = read_zip_entry(filename, "3D/3dmodel.model");
    if (!data)
        return result;

    ModelParser parser;
    try {
        Glib::Markup::ParseContext ctx(parser);
        ctx.parse(reinterpret_cast<const char *>(data->data()),
                  reinterpret_cast<const char *>(data->data() + data->size()));
        ctx.end_parse();
    }
    catch (const Glib::Error &) {
        return result;
    }

    if (parser.build_items.empty())
        return result;

    result.faces.emplace_back();
    auto &face_out = result.faces.back();
    face_out.color = {0.5, 0.5, 0.5};
    std::vector<glm::dvec3> normal_acc;

    for (const auto &[objid, transform] : parser.build_items) {
        std::set<std::string> visiting;
        resolve_object_into(parser.objects, objid, transform, parser.unit_scale, face_out, normal_acc, visiting);
    }

    if (face_out.vertices.empty()) {
        result.faces.clear();
        return result;
    }

    face_out.normals.reserve(normal_acc.size());
    for (const auto &n : normal_acc) {
        const auto len = glm::length(n);
        const auto nn = len > 1e-12 ? n / len : glm::dvec3(0, 0, 1);
        face_out.normals.emplace_back(nn.x, nn.y, nn.z);
    }

    // Center on the combined mesh's own bounding box, same reasoning (and
    // same fix) as stl_importer.cpp: a build item's transform can place
    // geometry far from this file's own local origin.
    glm::dvec3 bbox_min(std::numeric_limits<double>::max());
    glm::dvec3 bbox_max(std::numeric_limits<double>::lowest());
    for (const auto &v : face_out.vertices) {
        bbox_min.x = std::min(bbox_min.x, static_cast<double>(v.x));
        bbox_min.y = std::min(bbox_min.y, static_cast<double>(v.y));
        bbox_min.z = std::min(bbox_min.z, static_cast<double>(v.z));
        bbox_max.x = std::max(bbox_max.x, static_cast<double>(v.x));
        bbox_max.y = std::max(bbox_max.y, static_cast<double>(v.y));
        bbox_max.z = std::max(bbox_max.z, static_cast<double>(v.z));
    }
    const auto center = (bbox_min + bbox_max) * 0.5;
    for (auto &v : face_out.vertices) {
        v.x -= static_cast<float>(center.x);
        v.y -= static_cast<float>(center.y);
        v.z -= static_cast<float>(center.z);
    }

    return result;
}

} // namespace dune3d::ThreeMFImporter
