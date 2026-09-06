#include "demo_radio.h"

#include "legacy_nvs_migration.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define MIGRATION_NAMESPACE "passport_mig"
#define MIGRATION_KEY "feishu_clean"
#define MIGRATION_VERSION 1U

static const char *TAG = "demo_radio";

static bool s_nvs_ready;

static int legacy_migration_is_complete(void *context, bool *complete)
{
    (void)context;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(MIGRATION_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *complete = false;
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    uint8_t version = 0;
    err = nvs_get_u8(handle, MIGRATION_KEY, &version);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *complete = false;
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    *complete = version >= MIGRATION_VERSION;
    return ESP_OK;
}

static int legacy_migration_erase_namespace(void *context,
                                            const char *name_space)
{
    (void)context;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(name_space, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    nvs_close(handle);

    err = nvs_open(name_space, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_all(handle);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static int legacy_migration_mark_complete(void *context)
{
    (void)context;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(MIGRATION_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u8(handle, MIGRATION_KEY, MIGRATION_VERSION);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static const legacy_nvs_migration_ops_t LEGACY_MIGRATION_OPS = {
    .is_complete = legacy_migration_is_complete,
    .erase_namespace = legacy_migration_erase_namespace,
    .mark_complete = legacy_migration_mark_complete,
};

esp_err_t demo_radio_nvs_prepare(void)
{
    if (s_nvs_ready) return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        // 示例不能为了启动无线功能而擦除未来应用可能已经保存的数据。
        ESP_LOGE(TAG, "NVS 初始化失败: %s;未自动擦除分区", esp_err_to_name(err));
        return err;
    }
    err = (esp_err_t)legacy_nvs_migration_run(&LEGACY_MIGRATION_OPS, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "旧 Feishu NVS 数据迁移清理失败: %s",
                 esp_err_to_name(err));
        return err;
    }
    s_nvs_ready = true;
    return ESP_OK;
}
