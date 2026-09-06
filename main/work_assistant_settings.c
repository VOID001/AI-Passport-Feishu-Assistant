#include "work_assistant_settings.h"

static const uint32_t AUTO_OFF_TIMEOUTS_MS[WORK_ASSISTANT_AUTO_OFF_COUNT] = {
    5000U,
    15000U,
    30000U,
    60000U,
    0U,
};

static const char *const AUTO_OFF_LABELS[WORK_ASSISTANT_AUTO_OFF_COUNT] = {
    "5s",
    "15s",
    "30s",
    "1min",
    "Never",
};

uint32_t work_assistant_auto_off_timeout_ms(work_assistant_auto_off_t setting)
{
    if (setting >= WORK_ASSISTANT_AUTO_OFF_COUNT) {
        setting = WORK_ASSISTANT_AUTO_OFF_1_MINUTE;
    }
    return AUTO_OFF_TIMEOUTS_MS[setting];
}

const char *work_assistant_auto_off_label(work_assistant_auto_off_t setting)
{
    if (setting >= WORK_ASSISTANT_AUTO_OFF_COUNT) {
        setting = WORK_ASSISTANT_AUTO_OFF_1_MINUTE;
    }
    return AUTO_OFF_LABELS[setting];
}

work_assistant_auto_off_t work_assistant_auto_off_next(
    work_assistant_auto_off_t setting)
{
    if (setting >= WORK_ASSISTANT_AUTO_OFF_COUNT - 1) {
        return WORK_ASSISTANT_AUTO_OFF_5_SECONDS;
    }
    return (work_assistant_auto_off_t)(setting + 1);
}
