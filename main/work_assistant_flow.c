#include "work_assistant_flow.h"

void work_assistant_flow_init(work_assistant_flow_t *flow)
{
    if (!flow) return;
    flow->screen = WORK_ASSISTANT_SCREEN_LAUNCH;
}

bool work_assistant_flow_confirm(work_assistant_flow_t *flow)
{
    if (!flow || flow->screen != WORK_ASSISTANT_SCREEN_LAUNCH) return false;
    flow->screen = WORK_ASSISTANT_SCREEN_BLE;
    return true;
}

void work_assistant_flow_show_home(work_assistant_flow_t *flow)
{
    if (!flow) return;
    flow->screen = WORK_ASSISTANT_SCREEN_HOME;
}
