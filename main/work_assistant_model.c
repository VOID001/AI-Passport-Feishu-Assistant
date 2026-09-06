#include "work_assistant_model.h"

#include <stdio.h>
#include <string.h>

static int compare_event(const work_assistant_event_t *left,
                         const work_assistant_event_t *right)
{
    if (left->start_minute != right->start_minute) {
        return left->start_minute < right->start_minute ? -1 : 1;
    }
    return strcmp(left->title, right->title);
}

static int compare_task(const work_assistant_task_t *left,
                        const work_assistant_task_t *right)
{
    if (left->due_at_epoch != right->due_at_epoch) {
        return left->due_at_epoch > right->due_at_epoch ? -1 : 1;
    }
    return strcmp(left->title, right->title);
}

size_t work_assistant_prepare_today(work_assistant_event_t *events, size_t count,
                                    uint32_t today_key)
{
    if (!events) return 0;

    size_t kept = 0;
    for (size_t i = 0; i < count; i++) {
        if (events[i].day_key == today_key) events[kept++] = events[i];
    }

    /* The bounded event list is small, so an allocation-free insertion sort is enough. */
    for (size_t i = 1; i < kept; i++) {
        work_assistant_event_t current = events[i];
        size_t j = i;
        while (j > 0 && compare_event(&current, &events[j - 1]) < 0) {
            events[j] = events[j - 1];
            j--;
        }
        events[j] = current;
    }
    return kept;
}

void work_assistant_sort_tasks(work_assistant_task_t *tasks, size_t count)
{
    if (!tasks) return;
    for (size_t i = 1; i < count; i++) {
        work_assistant_task_t current = tasks[i];
        size_t j = i;
        while (j > 0 && compare_task(&current, &tasks[j - 1]) < 0) {
            tasks[j] = tasks[j - 1];
            j--;
        }
        tasks[j] = current;
    }
}

work_assistant_window_t work_assistant_window(size_t item_count, size_t first,
                                              size_t capacity)
{
    work_assistant_window_t window = { 0, 0 };
    if (item_count == 0 || capacity == 0) return window;

    if (first >= item_count) first = item_count - 1;
    if (first + capacity > item_count && item_count > capacity) {
        first = item_count - capacity;
    }
    window.first = first;
    window.count = item_count - first;
    if (window.count > capacity) window.count = capacity;
    return window;
}

bool work_assistant_format_time(uint16_t minute, char out[6])
{
    if (!out || minute >= 24U * 60U) return false;
    int written = snprintf(out, 6, "%02u:%02u", minute / 60U, minute % 60U);
    return written == 5;
}

size_t work_assistant_time_target(const work_assistant_event_t *events,
                                  size_t count, uint16_t minute)
{
    if (!events || minute >= 24U * 60U) return WORK_ASSISTANT_NO_EVENT;

    for (size_t index = 0; index < count; index++) {
        const work_assistant_event_t *event = &events[index];
        if (!event->all_day && event->start_minute <= minute &&
            minute < event->end_minute) {
            return index;
        }
    }
    for (size_t index = 0; index < count; index++) {
        const work_assistant_event_t *event = &events[index];
        if (!event->all_day && event->start_minute > minute) return index;
    }
    return WORK_ASSISTANT_NO_EVENT;
}

void work_assistant_load_demo(work_assistant_summary_t *summary)
{
    if (!summary) return;

    *summary = (work_assistant_summary_t) {
        .account_name = "Feishu User",
        .today_key = 20260903,
        .generated_at_epoch = 1788429600,
        .utc_offset_minutes = 480,
        .overdue_task_count = 4,
        .unread_message_count = 12,
        .event_count = 6,
        .events = {
            {
                .day_key = 20260903,
                .start_minute = 9 * 60 + 30,
                .end_minute = 10 * 60,
                .title = "Team stand-up",
                .location = "Meeting room A",
            },
            {
                .day_key = 20260903,
                .start_minute = 10 * 60 + 30,
                .end_minute = 11 * 60,
                .completed = true,
                .title = "Project review",
            },
            {
                .day_key = 20260903,
                .start_minute = 13 * 60,
                .end_minute = 14 * 60,
                .title = "Focus time",
            },
            {
                .day_key = 20260903,
                .start_minute = 15 * 60 + 30,
                .end_minute = 16 * 60,
                .title = "Customer sync",
                .location = "Video meeting",
            },
            {
                .day_key = 20260903,
                .start_minute = 17 * 60,
                .end_minute = 17 * 60 + 30,
                .completed = true,
                .title = "Daily wrap-up",
            },
            {
                .day_key = 20260904,
                .start_minute = 9 * 60,
                .end_minute = 10 * 60,
                .title = "Tomorrow only",
            },
        },
        .task_count = 4,
        .tasks = {
            {
                .guid = "demo-weekly-report",
                .due_at_epoch = 1788426000,
                .title = "Submit weekly report",
            },
            {
                .guid = "demo-proposal-review",
                .due_at_epoch = 1788343200,
                .title = "Review project proposal",
            },
            {
                .guid = "demo-delivery-checklist",
                .due_at_epoch = 1787904000,
                .all_day = true,
                .title = "Update delivery checklist",
            },
            {
                .guid = "demo-meeting-notes",
                .due_at_epoch = 1787212800,
                .all_day = true,
                .title = "Archive meeting notes",
            },
        },
    };
}
