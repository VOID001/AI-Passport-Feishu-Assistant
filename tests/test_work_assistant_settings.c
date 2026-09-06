#include <assert.h>
#include <string.h>

#include "work_assistant_settings.h"

int main(void)
{
    assert(work_assistant_auto_off_timeout_ms(
               WORK_ASSISTANT_AUTO_OFF_5_SECONDS) == 5000U);
    assert(work_assistant_auto_off_timeout_ms(
               WORK_ASSISTANT_AUTO_OFF_15_SECONDS) == 15000U);
    assert(work_assistant_auto_off_timeout_ms(
               WORK_ASSISTANT_AUTO_OFF_30_SECONDS) == 30000U);
    assert(work_assistant_auto_off_timeout_ms(
               WORK_ASSISTANT_AUTO_OFF_1_MINUTE) == 60000U);
    assert(work_assistant_auto_off_timeout_ms(
               WORK_ASSISTANT_AUTO_OFF_NEVER) == 0U);
    assert(!strcmp(work_assistant_auto_off_label(
                       WORK_ASSISTANT_AUTO_OFF_1_MINUTE), "1min"));
    assert(work_assistant_auto_off_next(WORK_ASSISTANT_AUTO_OFF_NEVER) ==
           WORK_ASSISTANT_AUTO_OFF_5_SECONDS);
    assert(work_assistant_auto_off_next(WORK_ASSISTANT_AUTO_OFF_15_SECONDS) ==
           WORK_ASSISTANT_AUTO_OFF_30_SECONDS);
    return 0;
}
