#include "legacy_nvs_migration.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

enum {
    TEST_OK = 0,
    TEST_READ_FAILED = 11,
    TEST_AGENT_FAILED = 12,
    TEST_USER_FAILED = 13,
    TEST_MARK_FAILED = 14,
};

typedef struct {
    bool complete;
    int failure;
    char calls[16];
    size_t call_count;
} fake_store_t;

static void record_call(fake_store_t *store, char call)
{
    assert(store->call_count + 1 < sizeof(store->calls));
    store->calls[store->call_count++] = call;
    store->calls[store->call_count] = '\0';
}

static int fake_is_complete(void *context, bool *complete)
{
    fake_store_t *store = context;
    record_call(store, 'R');
    if (store->failure == TEST_READ_FAILED) return TEST_READ_FAILED;
    *complete = store->complete;
    return TEST_OK;
}

static int fake_erase_namespace(void *context, const char *name_space)
{
    fake_store_t *store = context;
    if (strcmp(name_space, LEGACY_FEISHU_AGENT_NAMESPACE) == 0) {
        record_call(store, 'A');
        if (store->failure == TEST_AGENT_FAILED) return TEST_AGENT_FAILED;
    } else {
        assert(strcmp(name_space, LEGACY_FEISHU_USER_NAMESPACE) == 0);
        record_call(store, 'U');
        if (store->failure == TEST_USER_FAILED) return TEST_USER_FAILED;
    }
    return TEST_OK;
}

static int fake_mark_complete(void *context)
{
    fake_store_t *store = context;
    record_call(store, 'M');
    if (store->failure == TEST_MARK_FAILED) return TEST_MARK_FAILED;
    store->complete = true;
    return TEST_OK;
}

static const legacy_nvs_migration_ops_t TEST_OPS = {
    .is_complete = fake_is_complete,
    .erase_namespace = fake_erase_namespace,
    .mark_complete = fake_mark_complete,
};

static void assert_calls(const fake_store_t *store, const char *expected)
{
    assert(strcmp(store->calls, expected) == 0);
}

static void test_success_is_persistent(void)
{
    fake_store_t store = { 0 };
    assert(legacy_nvs_migration_run(&TEST_OPS, &store) == TEST_OK);
    assert(store.complete);
    assert_calls(&store, "RAUM");

    store.call_count = 0;
    store.calls[0] = '\0';
    assert(legacy_nvs_migration_run(&TEST_OPS, &store) == TEST_OK);
    assert_calls(&store, "R");
}

static void test_completed_migration_skips_erases(void)
{
    fake_store_t store = { .complete = true };
    assert(legacy_nvs_migration_run(&TEST_OPS, &store) == TEST_OK);
    assert_calls(&store, "R");
}

static void test_read_failure_stops_migration(void)
{
    fake_store_t store = { .failure = TEST_READ_FAILED };
    assert(legacy_nvs_migration_run(&TEST_OPS, &store) == TEST_READ_FAILED);
    assert(!store.complete);
    assert_calls(&store, "R");
}

static void assert_failure_retries(int failure, const char *first_calls)
{
    fake_store_t store = { .failure = failure };
    assert(legacy_nvs_migration_run(&TEST_OPS, &store) == failure);
    assert(!store.complete);
    assert_calls(&store, first_calls);

    store.failure = TEST_OK;
    store.call_count = 0;
    store.calls[0] = '\0';
    assert(legacy_nvs_migration_run(&TEST_OPS, &store) == TEST_OK);
    assert(store.complete);
    assert_calls(&store, "RAUM");
}

static void test_failures_retry_without_completion_marker(void)
{
    assert_failure_retries(TEST_AGENT_FAILED, "RA");
    assert_failure_retries(TEST_USER_FAILED, "RAU");
    assert_failure_retries(TEST_MARK_FAILED, "RAUM");
}

int main(void)
{
    test_success_is_persistent();
    test_completed_migration_skips_erases();
    test_read_failure_stops_migration();
    test_failures_retry_without_completion_marker();
    return 0;
}
