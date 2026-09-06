"""Convert lark-cli work data to the bounded passport summary schema."""

from __future__ import annotations

from datetime import date, datetime, time, timedelta, timezone, tzinfo
from typing import Any, Dict, Iterable, List, Optional
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

from .protocol import (
    MAX_EVENT_TITLE_BYTES,
    MAX_EVENTS,
    MAX_TASK_TITLE_BYTES,
    MAX_TASKS,
    PROTOCOL_VERSION,
)

MAX_USER_NAME_BYTES = 23
MAX_TITLE_BYTES = MAX_EVENT_TITLE_BYTES
MAX_COUNTER = 65535
MIN_UTC_OFFSET_MINUTES = -14 * 60
MAX_UTC_OFFSET_MINUTES = 14 * 60


class SummaryError(ValueError):
    """The agenda cannot be represented by the summary schema."""


def truncate_utf8(value: Any, maximum_bytes: int, fallback: str) -> str:
    """Normalize whitespace and truncate without splitting a UTF-8 code point."""
    text = " ".join(value.split()) if isinstance(value, str) else ""
    if not text:
        text = fallback
    encoded = text.encode("utf-8")
    if len(encoded) <= maximum_bytes:
        return text
    truncated = encoded[:maximum_bytes].decode("utf-8", errors="ignore")
    if truncated:
        return truncated
    return fallback.encode("utf-8")[:maximum_bytes].decode("utf-8", errors="ignore")


def _counter(value: Any, field: str) -> int:
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or not 0 <= value <= MAX_COUNTER
    ):
        raise SummaryError(f"{field} must be between 0 and {MAX_COUNTER}")
    return value


def _zone(value: Any, fallback: tzinfo) -> tzinfo:
    if isinstance(value, str) and value:
        try:
            return ZoneInfo(value)
        except ZoneInfoNotFoundError:
            pass
    return fallback


def _parse_datetime(value: Any, fallback_zone: tzinfo) -> datetime:
    if isinstance(value, (int, float)):
        timestamp = float(value)
        if abs(timestamp) > 10_000_000_000:
            timestamp /= 1000
        return datetime.fromtimestamp(timestamp, tz=timezone.utc)
    if not isinstance(value, str) or not value:
        raise SummaryError("event time is missing")
    if value.isdigit():
        timestamp = int(value)
        if timestamp > 10_000_000_000:
            timestamp /= 1000
        return datetime.fromtimestamp(timestamp, tz=timezone.utc)

    normalized = value[:-1] + "+00:00" if value.endswith("Z") else value
    try:
        parsed = datetime.fromisoformat(normalized)
    except ValueError as exc:
        raise SummaryError("event datetime is invalid") from exc
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=fallback_zone)
    return parsed


def _time_info(event: Dict[str, Any], key: str) -> Dict[str, Any]:
    value = event.get(key)
    if not isinstance(value, dict):
        raise SummaryError(f"event {key} is missing")
    return value


def _date_value(info: Dict[str, Any]) -> Optional[date]:
    value = info.get("date")
    if not isinstance(value, str):
        return None
    try:
        return date.fromisoformat(value)
    except ValueError as exc:
        raise SummaryError("all-day event date is invalid") from exc


def _datetime_value(info: Dict[str, Any], local_zone: tzinfo) -> datetime:
    value = info.get("datetime", info.get("timestamp"))
    event_zone = _zone(info.get("timezone"), local_zone)
    return _parse_datetime(value, event_zone).astimezone(local_zone)


def _convert_event(
    event: Dict[str, Any],
    current: datetime,
    day_start: datetime,
    next_day: datetime,
) -> Optional[Dict[str, Any]]:
    if event.get("status") == "cancelled":
        return None

    local_zone = current.tzinfo
    assert local_zone is not None
    start_info = _time_info(event, "start_time")
    end_info = _time_info(event, "end_time")
    start_date = _date_value(start_info)
    end_date = _date_value(end_info)
    title = truncate_utf8(event.get("summary"), MAX_TITLE_BYTES, "忙碌")

    if start_date is not None:
        final_date = end_date or start_date
        if not (start_date <= current.date() <= final_date):
            return None
        return {
            "title": title,
            "start_minute": 0,
            "end_minute": 24 * 60,
            "all_day": True,
            "completed": current.date() > final_date,
        }

    event_start = _datetime_value(start_info, local_zone)
    event_end = _datetime_value(end_info, local_zone)
    if event_end <= event_start:
        raise SummaryError("event end must be after its start")
    if event_end <= day_start or event_start >= next_day:
        return None

    clipped_start = max(event_start, day_start)
    clipped_end = min(event_end, next_day)
    start_minute = int((clipped_start - day_start).total_seconds() // 60)
    end_minute = int((clipped_end - day_start).total_seconds() // 60)
    return {
        "title": title,
        "start_minute": start_minute,
        "end_minute": end_minute,
        "all_day": False,
        "completed": event_end <= current,
    }


def _convert_task(
    task: Dict[str, Any], current: datetime
) -> Optional[Dict[str, Any]]:
    if task.get("status") == "done" or task.get("completed") is True:
        return None
    due = task.get("due")
    if isinstance(due, dict):
        due_value = due.get("timestamp")
        all_day = due.get("is_all_day") is True
    else:
        due_value = task.get("due_at")
        all_day = task.get("is_all_day") is True
    if due_value in (None, ""):
        return None

    local_zone = current.tzinfo
    assert local_zone is not None
    due_at = _parse_datetime(due_value, local_zone).astimezone(local_zone)
    window_start = current.date() - timedelta(days=29)
    if not window_start <= due_at.date() <= current.date():
        return None
    due_at_epoch = int(due_at.timestamp())
    if not 0 <= due_at_epoch <= 0xFFFFFFFF:
        raise SummaryError("task due time is outside the supported range")
    guid = task.get("guid", task.get("task_guid"))
    if not isinstance(guid, str) or not guid or len(guid) >= 64:
        raise SummaryError("task guid is missing or invalid")
    return {
        "guid": guid,
        "title": truncate_utf8(
            task.get("summary", task.get("title")),
            MAX_TASK_TITLE_BYTES,
            "未命名待办",
        ),
        "due_at_epoch": due_at_epoch,
        "all_day": all_day,
    }


def build_summary(
    agenda_events: Iterable[Dict[str, Any]],
    user_name: str,
    now: Optional[datetime] = None,
    *,
    tasks: Iterable[Dict[str, Any]] = (),
    avatar_rgb565: str = "",
    overdue_task_count: Optional[int] = None,
    unread_message_count: int = 0,
) -> Dict[str, Any]:
    """Build the complete protocol-v4 summary for the ESP32-C3 receiver."""
    current = now or datetime.now().astimezone()
    if current.tzinfo is None:
        current = current.replace(tzinfo=timezone.utc)
    local_zone = current.tzinfo
    assert local_zone is not None
    if not isinstance(avatar_rgb565, str):
        raise SummaryError("avatar_rgb565 must be a string")

    try:
        generated_at_epoch = int(current.timestamp())
    except (OSError, OverflowError, ValueError) as exc:
        raise SummaryError("generation time is outside the supported range") from exc
    if not 0 <= generated_at_epoch <= 0xFFFFFFFF:
        raise SummaryError("generation time is outside the supported range")

    utc_offset = current.utcoffset()
    if utc_offset is None or utc_offset.total_seconds() % 60:
        raise SummaryError("UTC offset must be minute-aligned")
    utc_offset_minutes = int(utc_offset.total_seconds() // 60)
    if not MIN_UTC_OFFSET_MINUTES <= utc_offset_minutes <= MAX_UTC_OFFSET_MINUTES:
        raise SummaryError("UTC offset is outside the supported range")

    day_start = datetime.combine(current.date(), time.min, tzinfo=local_zone)
    next_day = datetime.combine(
        current.date() + timedelta(days=1), time.min, tzinfo=local_zone
    )
    converted: List[Dict[str, Any]] = []
    for event in agenda_events:
        try:
            item = _convert_event(event, current, day_start, next_day)
        except SummaryError:
            raise
        except (TypeError, ValueError, OverflowError) as exc:
            raise SummaryError("event contains an invalid value") from exc
        if item is not None:
            converted.append(item)

    converted.sort(
        key=lambda item: (
            0 if item["all_day"] else 1,
            item["start_minute"],
            item["title"],
        )
    )
    converted_tasks: List[Dict[str, Any]] = []
    for task in tasks:
        try:
            item = _convert_task(task, current)
        except SummaryError:
            raise
        except (TypeError, ValueError, OverflowError) as exc:
            raise SummaryError("task contains an invalid value") from exc
        if item is not None:
            converted_tasks.append(item)
    converted_tasks.sort(
        key=lambda item: (item["due_at_epoch"], item["title"]), reverse=True
    )
    generated_at = current.astimezone(timezone.utc).isoformat(timespec="seconds")
    generated_at = generated_at.replace("+00:00", "Z")
    return {
        "version": PROTOCOL_VERSION,
        "generated_at": generated_at,
        "generated_at_epoch": generated_at_epoch,
        "utc_offset_minutes": utc_offset_minutes,
        "user_name": truncate_utf8(
            user_name, MAX_USER_NAME_BYTES, "Feishu User"
        ),
        "avatar_rgb565": avatar_rgb565,
        "date": current.date().isoformat(),
        "events": converted[:MAX_EVENTS],
        "tasks": converted_tasks[:MAX_TASKS],
        "overdue_task_count": _counter(
            len(converted_tasks)
            if overdue_task_count is None
            else overdue_task_count,
            "overdue_task_count",
        ),
        "unread_message_count": _counter(
            unread_message_count, "unread_message_count"
        ),
    }
