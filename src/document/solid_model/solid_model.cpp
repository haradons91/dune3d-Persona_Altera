#include "solid_model.hpp"
#include "solid_model_occ.hpp"
#include "document/document.hpp"
#include "document/group/group.hpp"
#include "document/group/igroup_solid_model.hpp"
#include "document/group/group_extrude.hpp"
#include "document/group/group_sketch.hpp"
#include "document/entity/entity_step.hpp"
#include "util/debug.hpp"
#include <format>

namespace dune3d {

SolidModel::~SolidModel() = default;

const IGroupSolidModel *SolidModel::get_last_solid_model_group(const Document &doc, const Group &group,
                                                               IncludeGroup include_group)
{
    const bool trace = debug_enabled(DebugCategory::MODEL);
    if (trace) {
        debug_log(DebugCategory::MODEL,
                  std::format("find-last target={} type={} include_group={}", static_cast<std::string>(group.m_uuid),
                              static_cast<int>(group.get_type()), static_cast<int>(include_group)));
        for (auto gr : doc.get_groups_sorted()) {
            std::string line = std::format("  index={} uuid={} type={} name={} body={}", gr->get_index(),
                                            static_cast<std::string>(gr->m_uuid), static_cast<int>(gr->get_type()),
                                            gr->m_name, gr->m_body.has_value());
            bool has_step = false;
            for (const auto &[entity_uuid, entity] : doc.m_entities) {
                if (entity->m_group != gr->m_uuid)
                    continue;
                if (const auto *step = dynamic_cast<const EntitySTEP *>(entity.get())) {
                    has_step = true;
                    line += std::format(" step={} include={} imported={}", static_cast<std::string>(entity_uuid),
                                        step->m_include_in_solid_model, step->m_imported != nullptr);
                }
            }
            if (!has_step)
                line += " step=none";
            debug_log(DebugCategory::MODEL, line);
        }
    }

    const IGroupSolidModel *last_solid_model_group = nullptr;
    auto this_body = &group.find_body(doc).body;

    for (auto gr : doc.get_groups_sorted()) {
        if (include_group == IncludeGroup::NO && gr->m_uuid == group.m_uuid)
            break;
        if (auto gr_solid = dynamic_cast<const IGroupSolidModel *>(gr)) {
            if (auto solid_model = dynamic_cast<const SolidModelOcc *>(gr_solid->get_solid_model())) {
                auto body = &gr->find_body(doc).body;
                if (body != this_body)
                    continue;
                if (trace)
                    debug_log(DebugCategory::MODEL,
                              std::format(" candidate={} type={} model_shape={} acc_shape={} operation={}",
                                          static_cast<std::string>(gr->m_uuid), static_cast<int>(gr->get_type()),
                                          !solid_model->m_shape.IsNull(), !solid_model->m_shape_acc.IsNull(),
                                          static_cast<int>(gr_solid->get_operation())));
                if (!solid_model->m_shape_acc.IsNull())
                    last_solid_model_group = gr_solid;
            }
        }
        if (include_group == IncludeGroup::YES && gr->m_uuid == group.m_uuid)
            break;
    }

    // A STEP body may have been imported after a sketch/extrusion already
    // existed.  For a Difference extrusion, allow that independent imported
    // body to be the boolean argument when no earlier solid was found.
    if (!last_solid_model_group) {
        const auto *extrude = dynamic_cast<const GroupExtrude *>(&group);
        if (extrude && extrude->m_operation == IGroupSolidModel::Operation::DIFFERENCE) {
            bool after_target = false;
            for (auto gr : doc.get_groups_sorted()) {
                if (gr->m_uuid == group.m_uuid) {
                    after_target = true;
                    continue;
                }
                if (!after_target)
                    continue;
                const auto *sketch = dynamic_cast<const GroupSketch *>(gr);
                if (!sketch || &sketch->find_body(doc).body != this_body)
                    continue;
                bool has_imported_body = false;
                for (const auto &[entity_uuid, entity] : doc.m_entities) {
                    if (entity->m_group != gr->m_uuid)
                        continue;
                    if (const auto *step = dynamic_cast<const EntitySTEP *>(entity.get()); step
                        && step->m_include_in_solid_model && step->m_imported) {
                        has_imported_body = true;
                        break;
                    }
                }
                if (has_imported_body) {
                    if (auto solid_model = dynamic_cast<const SolidModelOcc *>(sketch->get_solid_model()); solid_model
                        && !solid_model->m_shape_acc.IsNull()) {
                        last_solid_model_group = sketch;
                        if (trace)
                            debug_log(DebugCategory::MODEL,
                                      std::format(" future-step-fallback={}", static_cast<std::string>(gr->m_uuid)));
                        break;
                    }
                }
            }
        }
    }

    if (trace)
        debug_log(DebugCategory::MODEL,
                  std::format(" selected={}",
                              last_solid_model_group
                                      ? static_cast<std::string>(
                                              dynamic_cast<const Group *>(last_solid_model_group)->m_uuid)
                                      : std::string{"none"}));
    return last_solid_model_group;
}

const SolidModel *SolidModel::get_last_solid_model(const Document &doc, const Group &group, IncludeGroup include_group)
{
    auto gr = get_last_solid_model_group(doc, group, include_group);
    if (gr)
        return gr->get_solid_model();
    else
        return nullptr;
}

} // namespace dune3d
