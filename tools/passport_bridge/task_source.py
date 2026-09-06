"""Read recent incomplete Feishu tasks through the local lark-cli identity."""

from __future__ import annotations

import json
import subprocess
from datetime import datetime, timedelta, timezone
from typing import Any, Callable, Dict, List, Optional


class TaskSourceError(RuntimeError):
    """A sanitized task-source failure safe to show in bridge logs."""


def complete_task_command(guid: str, executable: str = "lark-cli") -> List[str]:
    if not isinstance(guid, str) or not guid or len(guid) >= 64:
        raise TaskSourceError("task completion rejected (invalid-guid)")
    return [executable, "task", "+complete", "--as", "user", "--task-id", guid]


def complete_task(
    guid: str, executable: str = "lark-cli", timeout: float = 30.0,
    runner: Callable[..., subprocess.CompletedProcess] = subprocess.run,
) -> bool:
    try:
        result = runner(complete_task_command(guid, executable), check=False,
                        capture_output=True, text=True, timeout=timeout)
    except (FileNotFoundError, subprocess.TimeoutExpired, OSError) as exc:
        raise _failure("complete-unavailable") from exc
    if result.returncode != 0:
        raise _failure(f"complete-exit-{result.returncode}")
    return True


def task_command(executable: str = "lark-cli") -> List[str]:
    """Return the fixed, credential-free recent-task command."""
    return [
        executable,
        "task",
        "+get-my-tasks",
        "--as",
        "user",
        "--complete=false",
        "--due-start=-30d",
        "--due-end=+1d",
        "--page-all",
        "--json",
    ]


def _failure(status: str) -> TaskSourceError:
    return TaskSourceError(f"lark-cli task query failed (status: {status})")


def _parse_due_at(value: Any, local_zone: Any) -> Optional[datetime]:
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)):
        timestamp = float(value)
        if abs(timestamp) > 10_000_000_000:
            timestamp /= 1000
        try:
            return datetime.fromtimestamp(timestamp, tz=timezone.utc)
        except (OSError, OverflowError, ValueError):
            return None
    if not isinstance(value, str) or not value.strip():
        return None
    text = value.strip()
    if text.lstrip("-").isdigit():
        return _parse_due_at(int(text), local_zone)
    normalized = text[:-1] + "+00:00" if text.endswith("Z") else text
    try:
        parsed = datetime.fromisoformat(normalized)
    except ValueError:
        return None
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=local_zone)
    return parsed


def _task_items(document: Any) -> Optional[List[Dict[str, Any]]]:
    if not isinstance(document, dict) or document.get("ok") is not True:
        return None
    data = document.get("data")
    if isinstance(data, list):
        items = data
    elif isinstance(data, dict):
        items = data.get("items", data.get("tasks"))
    else:
        return None
    if not isinstance(items, list) or not all(isinstance(item, dict) for item in items):
        return None
    return items


def _due_value(item: Dict[str, Any]) -> Any:
    due = item.get("due")
    if isinstance(due, dict):
        return due.get("timestamp")
    return item.get("due_at")


def parse_recent_tasks(raw_output: str, now: datetime) -> List[Dict[str, Any]]:
    """Return incomplete tasks due today or in the previous 29 local dates."""
    if now.tzinfo is None:
        raise ValueError("now must include a timezone")
    try:
        document = json.loads(raw_output)
    except (TypeError, json.JSONDecodeError) as exc:
        raise _failure("invalid-json") from exc
    items = _task_items(document)
    if items is None:
        raise _failure("invalid-response")

    window_start = now.date() - timedelta(days=29)
    recent = []
    for item in items:
        if item.get("status") == "done" or item.get("completed") is True:
            continue
        due_at = _parse_due_at(_due_value(item), now.tzinfo)
        if due_at is None:
            continue
        due_date = due_at.astimezone(now.tzinfo).date()
        if window_start <= due_date <= now.date():
            recent.append(item)
    return recent


def run_recent_tasks(
    now: datetime,
    executable: str = "lark-cli",
    timeout: float = 30.0,
    runner: Callable[..., subprocess.CompletedProcess] = subprocess.run,
) -> List[Dict[str, Any]]:
    """Run the bounded-date task query and return sanitized task dictionaries."""
    try:
        result = runner(
            task_command(executable),
            check=False,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except FileNotFoundError as exc:
        raise _failure("not-installed") from exc
    except subprocess.TimeoutExpired as exc:
        raise _failure("timeout") from exc
    except OSError as exc:
        raise _failure("unavailable") from exc
    if result.returncode != 0:
        raise _failure(f"exit-{result.returncode}")
    return parse_recent_tasks(result.stdout, now)
