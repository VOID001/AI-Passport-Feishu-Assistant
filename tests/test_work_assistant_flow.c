#include "work_assistant_flow.h"

#include <assert.h>

int main(void)
{
    work_assistant_flow_t flow;
    work_assistant_flow_init(&flow);
    assert(flow.screen == WORK_ASSISTANT_SCREEN_LAUNCH);

    assert(work_assistant_flow_confirm(&flow));
    assert(flow.screen == WORK_ASSISTANT_SCREEN_BLE);
    assert(!work_assistant_flow_confirm(&flow));
    work_assistant_flow_show_home(&flow);
    assert(flow.screen == WORK_ASSISTANT_SCREEN_HOME);
    return 0;
}
