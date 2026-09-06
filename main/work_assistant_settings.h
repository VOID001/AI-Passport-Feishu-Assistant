#pragma once

#include <stdint.h>

typedef enum {
    WORK_ASSISTANT_AUTO_OFF_5_SECONDS = 0,
    WORK_ASSISTANT_AUTO_OFF_15_SECONDS,
    WORK_ASSISTANT_AUTO_OFF_30_SECONDS,
    WORK_ASSISTANT_AUTO_OFF_1_MINUTE,
    WORK_ASSISTANT_AUTO_OFF_NEVER,
    WORK_ASSISTANT_AUTO_OFF_COUNT,
} work_assistant_auto_off_t;

/* Returns zero when automatic screen-off is disabled. */
uint32_t work_assistant_auto_off_timeout_ms(work_assistant_auto_off_t setting);
const char *work_assistant_auto_off_label(work_assistant_auto_off_t setting);
work_assistant_auto_off_t work_assistant_auto_off_next(
    work_assistant_auto_off_t setting);
