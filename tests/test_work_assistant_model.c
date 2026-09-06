#include <assert.h>
#include <string.h>

#include "work_assistant_model.h"

int main(void)
{
    work_assistant_event_t events[] = {
        { .day_key = 20260904, .start_minute = 540, .end_minute = 600,
          .title = "Tomorrow" },
        { .day_key = 20260903, .start_minute = 900, .end_minute = 930,
          .title = "Later" },
        { .day_key = 20260903, .start_minute = 510, .end_minute = 540,
          .completed = true, .title = "Earlier" },
        { .day_key = 20260903, .start_minute = 510, .end_minute = 540,
          .title = "Alpha" },
    };

    size_t count = work_assistant_prepare_today(events, 4, 20260903);
    assert(count == 3);
    assert(strcmp(events[0].title, "Alpha") == 0);
    assert(strcmp(events[1].title, "Earlier") == 0);
    assert(strcmp(events[2].title, "Later") == 0);
    assert(events[1].completed);

    work_assistant_window_t window = work_assistant_window(5, 0, 3);
    assert(window.first == 0 && window.count == 3);
    window = work_assistant_window(5, 4, 3);
    assert(window.first == 2 && window.count == 3);
    window = work_assistant_window(0, 0, 3);
    assert(window.first == 0 && window.count == 0);

    char time[6] = { 0 };
    assert(work_assistant_format_time(9 * 60 + 5, time));
    assert(strcmp(time, "09:05") == 0);
    assert(!work_assistant_format_time(24 * 60, time));

    work_assistant_event_t timeline[] = {
        { .start_minute = 0, .end_minute = 1440, .all_day = true,
          .title = "All day" },
        { .start_minute = 540, .end_minute = 600, .title = "First" },
        { .start_minute = 570, .end_minute = 630, .title = "Overlap" },
        { .start_minute = 660, .end_minute = 720, .title = "Next" },
    };
    assert(work_assistant_time_target(timeline, 4, 500) == 1);
    assert(work_assistant_time_target(timeline, 4, 575) == 1);
    assert(work_assistant_time_target(timeline, 4, 620) == 2);
    assert(work_assistant_time_target(timeline, 4, 650) == 3);
    assert(work_assistant_time_target(timeline, 4, 720) ==
           WORK_ASSISTANT_NO_EVENT);
    assert(work_assistant_time_target(timeline, 4, 1440) ==
           WORK_ASSISTANT_NO_EVENT);

    work_assistant_task_t tasks[] = {
        { .due_at_epoch = 100, .title = "Older" },
        { .due_at_epoch = 300, .title = "Newest" },
        { .due_at_epoch = 200, .title = "Middle" },
    };
    work_assistant_sort_tasks(tasks, 3);
    assert(strcmp(tasks[0].title, "Newest") == 0);
    assert(strcmp(tasks[1].title, "Middle") == 0);
    assert(strcmp(tasks[2].title, "Older") == 0);

    work_assistant_summary_t summary;
    work_assistant_load_demo(&summary);
    assert(summary.overdue_task_count == 4);
    assert(summary.unread_message_count == 12);
    assert(summary.event_count > 0);
    assert(summary.events[0].completed == false);
    assert(summary.events[1].completed == true);
    assert(summary.events[5].day_key != summary.today_key);
    assert(summary.task_count == 4);
    assert(summary.tasks[0].due_at_epoch > summary.tasks[1].due_at_epoch);
    return 0;
}
