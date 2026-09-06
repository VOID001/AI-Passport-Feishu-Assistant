#include "legacy_nvs_migration.h"

int legacy_nvs_migration_run(const legacy_nvs_migration_ops_t *ops,
                             void *context)
{
    if (!ops || !ops->is_complete || !ops->erase_namespace ||
        !ops->mark_complete) {
        return LEGACY_NVS_MIGRATION_INVALID_ARGUMENT;
    }

    bool complete = false;
    int result = ops->is_complete(context, &complete);
    if (result != 0 || complete) return result;

    result = ops->erase_namespace(context, LEGACY_FEISHU_AGENT_NAMESPACE);
    if (result != 0) return result;

    result = ops->erase_namespace(context, LEGACY_FEISHU_USER_NAMESPACE);
    if (result != 0) return result;

    return ops->mark_complete(context);
}
