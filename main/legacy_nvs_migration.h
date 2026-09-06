#pragma once

#include <stdbool.h>

#define LEGACY_FEISHU_AGENT_NAMESPACE "feishu_agent"
#define LEGACY_FEISHU_USER_NAMESPACE "feishu_user"
#define LEGACY_NVS_MIGRATION_INVALID_ARGUMENT (-1)

typedef struct {
    int (*is_complete)(void *context, bool *complete);
    int (*erase_namespace)(void *context, const char *name_space);
    int (*mark_complete)(void *context);
} legacy_nvs_migration_ops_t;

// Returns the first operation error. The completion marker is written only
// after both legacy namespaces have been erased successfully.
int legacy_nvs_migration_run(const legacy_nvs_migration_ops_t *ops,
                             void *context);
