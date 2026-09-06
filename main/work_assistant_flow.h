#pragma once

#include <stdbool.h>

typedef enum {
    WORK_ASSISTANT_SCREEN_LAUNCH = 0,
    WORK_ASSISTANT_SCREEN_BLE,
    WORK_ASSISTANT_SCREEN_HOME,
} work_assistant_screen_t;

typedef struct {
    work_assistant_screen_t screen;
} work_assistant_flow_t;

void work_assistant_flow_init(work_assistant_flow_t *flow);

/* Returns true when Bluetooth pairing can begin. */
bool work_assistant_flow_confirm(work_assistant_flow_t *flow);
void work_assistant_flow_show_home(work_assistant_flow_t *flow);
