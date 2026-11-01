#include "properties.h"
#include <string.h>
static int name_valid(const char *s) {
    if (!s || !*s)
        return 0;
    for (size_t i = 0; i <= 4096; i++)
        if (!s[i])
            return 1;
    return 0;
}
FgStatus fg_tt_property(void *context, const FgPropertyPath *path, uint64_t *budget,
                        FgFactValue *value) {
    FgTtProperties *p = context;
    if (!p || !p->registry || !path || !budget || !value || !name_valid(path->object) ||
        path->field_count > FG_FACT_MAX_PATH || (path->field_count && !path->fields))
        return FG_STATUS_INVALID;
    for (size_t i = 0; i < path->field_count; i++)
        if (!name_valid(path->fields[i]))
            return FG_STATUS_INVALID;
    uint64_t names = (uint64_t)strlen(path->object) + 1;
    for (size_t i = 0; i < path->field_count; i++)
        names += (uint64_t)strlen(path->fields[i]) + 1;
    if (fg_budget_charge(budget, names) != FG_STATUS_OK)
        return FG_STATUS_BUDGET;
    FgObjectKey keys[4];
    FgObjectIdentity ids[4];
    keys[0] = p->leaf;
    for (size_t i = 0; i < 4; i++) {
        if (fg_budget_charge(budget, 1) != FG_STATUS_OK)
            return FG_STATUS_BUDGET;
        FgStatus status = fg_objects_resolve(p->registry, &keys[i], &ids[i]);
        if (status != FG_STATUS_OK)
            return status;
        if (i < 3) {
            if (fg_budget_charge(budget, 1) != FG_STATUS_OK)
                return FG_STATUS_BUDGET;
            status = fg_objects_parent(p->registry, &keys[i], &keys[i + 1]);
            if (status != FG_STATUS_OK)
                return status;
            if (keys[i + 1].run != keys[0].run || keys[i + 1].launch != keys[0].launch)
                return FG_STATUS_INVALID;
        }
    }
    if (ids[0].kind < 1 || ids[0].kind > 3 || ids[1].kind != 6 || ids[2].kind != 5 ||
        ids[3].kind != 4 || ids[0].device != ids[1].device || ids[1].device != ids[2].device ||
        ids[0].core_x != ids[1].core_x || ids[0].core_y != ids[1].core_y || ids[1].local ||
        ids[2].local || ids[2].core_x || ids[2].core_y || ids[3].device || ids[3].local ||
        ids[3].core_x || ids[3].core_y)
        return FG_STATUS_INVALID;
    FgObjectKey parent;
    if (fg_budget_charge(budget, 1) != FG_STATUS_OK)
        return FG_STATUS_BUDGET;
    if (fg_objects_parent(p->registry, &keys[3], &parent) != FG_STATUS_NOT_FOUND)
        return FG_STATUS_INVALID;
    const char *leaf = ids[0].kind == 1 ? "cb" : ids[0].kind == 2 ? "noc" : "compute";
    const char *objects[] = {"tt_emule", "tt_emule.device", "tt_emule.device.core",
                             ids[0].kind == 1   ? "tt_emule.device.core.cb"
                             : ids[0].kind == 2 ? "tt_emule.device.core.noc"
                                                : "tt_emule.device.core.compute"};
    size_t level = 4;
    for (size_t i = 0; i < 4; i++)
        if (!strcmp(path->object, objects[i]))
            level = i;
    if (level == 4)
        return FG_STATUS_NOT_FOUND;
    FgFactValue result = {.kind = FG_FACT_RECORD};
    for (size_t i = 0; i < path->field_count; i++) {
        if (fg_budget_charge(budget, 1) != FG_STATUS_OK)
            return FG_STATUS_BUDGET;
        const char *field = path->fields[i];
        if (result.kind != FG_FACT_RECORD)
            return FG_STATUS_NOT_FOUND;
        if ((level == 0 && !strcmp(field, "device")) || (level == 1 && !strcmp(field, "core")) ||
            (level == 2 && !strcmp(field, leaf))) {
            level++;
            continue;
        }
        result.kind = FG_FACT_INTEGER;
        if (!strcmp(field, "id") && level != 2)
            result.integer.low = level == 0   ? keys[3].run
                                 : level == 1 ? ids[2].device
                                              : ids[0].local;
        else if (level == 2 && !strcmp(field, "x"))
            result.integer.low = ids[1].core_x;
        else if (level == 2 && !strcmp(field, "y"))
            result.integer.low = ids[1].core_y;
        else
            return FG_STATUS_NOT_FOUND;
    }
    *value = result;
    return FG_STATUS_OK;
}
