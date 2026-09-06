#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WORK_ASSISTANT_NAME_MAX 24
#define WORK_ASSISTANT_TITLE_MAX 256
#define WORK_ASSISTANT_LOCATION_MAX 28
#define WORK_ASSISTANT_MAX_EVENTS 16
#define WORK_ASSISTANT_TASK_TITLE_MAX 128
#define WORK_ASSISTANT_MAX_TASKS 16
#define WORK_ASSISTANT_TASK_GUID_MAX 64
#define WORK_ASSISTANT_AVATAR_SIZE 32
#define WORK_ASSISTANT_AVATAR_BYTES \
    (WORK_ASSISTANT_AVATAR_SIZE * WORK_ASSISTANT_AVATAR_SIZE * 2)
#define WORK_ASSISTANT_NO_EVENT SIZE_MAX

typedef struct {
    uint32_t day_key; /* YYYYMMDD in the account's selected time zone. */
    uint16_t start_minute;
    uint16_t end_minute;
    bool all_day;
    bool completed;
    char title[WORK_ASSISTANT_TITLE_MAX];
    char location[WORK_ASSISTANT_LOCATION_MAX];
} work_assistant_event_t;

typedef struct {
    char guid[WORK_ASSISTANT_TASK_GUID_MAX];
    uint32_t due_at_epoch;
    bool all_day;
    char title[WORK_ASSISTANT_TASK_TITLE_MAX];
} work_assistant_task_t;

typedef struct {
    char account_name[WORK_ASSISTANT_NAME_MAX];
    uint32_t today_key;
    uint32_t generated_at_epoch;
    int16_t utc_offset_minutes;
    uint16_t overdue_task_count;
    uint16_t unread_message_count;
    bool avatar_valid;
    uint8_t avatar_rgb565[WORK_ASSISTANT_AVATAR_BYTES];
    size_t event_count;
    work_assistant_event_t events[WORK_ASSISTANT_MAX_EVENTS];
    size_t task_count;
    work_assistant_task_t tasks[WORK_ASSISTANT_MAX_TASKS];
} work_assistant_summary_t;

typedef struct {
    size_t first;
    size_t count;
} work_assistant_window_t;

/* Keeps only today's events and orders them by start time without allocating. */
size_t work_assistant_prepare_today(work_assistant_event_t *events, size_t count,
                                    uint32_t today_key);

/* Orders incomplete recent tasks by due time, newest first. */
void work_assistant_sort_tasks(work_assistant_task_t *tasks, size_t count);

/* Clamps a list viewport to the number of visible rows. */
work_assistant_window_t work_assistant_window(size_t item_count, size_t first,
                                              size_t capacity);

/* Formats a minute offset after midnight as a fixed HH:MM string. */
bool work_assistant_format_time(uint16_t minute, char out[6]);

/* Selects the first active timed event, or the next timed event. */
size_t work_assistant_time_target(const work_assistant_event_t *events,
                                  size_t count, uint16_t minute);

/* Provides safe local-only content until a trusted companion syncs a summary. */
void work_assistant_load_demo(work_assistant_summary_t *summary);
