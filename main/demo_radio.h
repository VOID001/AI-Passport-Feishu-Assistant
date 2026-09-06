#pragma once

#include "esp_err.h"

// NimBLE 依赖 NVS；初始化时只清理带版本标记的旧 Feishu 数据，不在失败时
// 擦除整个 NVS 分区。
esp_err_t demo_radio_nvs_prepare(void);
