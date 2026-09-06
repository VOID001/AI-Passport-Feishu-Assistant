"""Read today's calendar agenda through the local lark-cli user identity."""

from __future__ import annotations

import json
import subprocess
from typing import Any, Callable, Dict, List, Optional


REQUIRED_SCOPE = "calendar:calendar:read calendar:calendar.event:read"
RECOVERY_COMMAND = f'lark-cli auth login --scope "{REQUIRED_SCOPE}"'


class CalendarSourceError(RuntimeError):
    """A sanitized calendar-source failure safe to show in bridge logs."""


def calendar_list_command(executable: str = "lark-cli") -> List[str]:
    """Return the owner-calendar discovery command."""
    return [
        executable,
        "calendar",
        "calendars",
        "list",
        "--as",
        "user",
        "--json",
        "--page-all",
    ]


def identity_command(executable: str = "lark-cli") -> List[str]:
    return [executable, "auth", "status", "--json", "--verify"]


def agenda_command(executable: str, calendar_id: str) -> List[str]:
    """Return the fixed, credential-free command used by the bridge."""
    return [
        executable,
        "calendar",
        "+agenda",
        "--calendar-id",
        calendar_id,
        "--as",
        "user",
        "--json",
    ]


def _failure(status: str) -> CalendarSourceError:
    return CalendarSourceError(
        f"lark-cli agenda failed (status: {status}). "
        f"Required scope: {REQUIRED_SCOPE}. Recovery: {RECOVERY_COMMAND}"
    )


def parse_agenda_output(raw_output: str) -> List[Dict[str, Any]]:
    """Extract event objects from the lark-cli JSON envelope."""
    try:
        document = json.loads(raw_output)
    except (TypeError, json.JSONDecodeError) as exc:
        raise _failure("invalid-json") from exc

    if isinstance(document, list):
        events = document
    elif isinstance(document, dict):
        if document.get("ok") is not True:
            raise _failure("api-error")
        events = document.get("data")
    else:
        raise _failure("invalid-response")

    if not isinstance(events, list) or not all(
        isinstance(event, dict) for event in events
    ):
        raise _failure("invalid-response")
    return events


def parse_owner_calendar_ids(raw_output: str) -> List[str]:
    """Return only calendars owned by the authenticated user."""
    try:
        document = json.loads(raw_output)
    except (TypeError, json.JSONDecodeError) as exc:
        raise _failure("invalid-calendar-list") from exc
    data = document.get("data") if isinstance(document, dict) else None
    calendars = data.get("calendar_list") if isinstance(data, dict) else None
    if (
        not isinstance(document, dict)
        or document.get("ok") is not True
        or not isinstance(calendars, list)
    ):
        raise _failure("invalid-calendar-list")

    result = []
    for calendar in calendars:
        if not isinstance(calendar, dict) or calendar.get("role") != "owner":
            continue
        calendar_id = calendar.get("calendar_id")
        if isinstance(calendar_id, str) and calendar_id:
            result.append(calendar_id)
    if not result:
        raise _failure("no-owner-calendar")
    return result


def parse_user_name(raw_output: str) -> str:
    """Read only the display name from the local user identity status."""
    try:
        document = json.loads(raw_output)
    except (TypeError, json.JSONDecodeError) as exc:
        raise _failure("invalid-user-identity") from exc
    identities = document.get("identities") if isinstance(document, dict) else None
    user = identities.get("user") if isinstance(identities, dict) else None
    name = user.get("userName") if isinstance(user, dict) else None
    if not isinstance(name, str) or not name.strip():
        raise _failure("missing-user-identity")
    return name.strip()


def _run(
    command: List[str],
    timeout: float,
    runner: Callable[..., subprocess.CompletedProcess],
) -> str:
    try:
        result = runner(
            command,
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
    return result.stdout


def run_agenda(
    executable: str = "lark-cli",
    timeout: float = 30.0,
    runner: Callable[..., subprocess.CompletedProcess] = subprocess.run,
    metadata: Optional[Dict[str, int]] = None,
) -> List[Dict[str, Any]]:
    """Return today's events from calendars owned by the current user."""
    owner_ids = parse_owner_calendar_ids(
        _run(calendar_list_command(executable), timeout, runner)
    )
    if metadata is not None:
        metadata["owner_calendar_count"] = len(owner_ids)
    merged: List[Dict[str, Any]] = []
    seen = set()
    for calendar_id in owner_ids:
        events = parse_agenda_output(
            _run(agenda_command(executable, calendar_id), timeout, runner)
        )
        for event in events:
            key = (
                event.get("event_id"),
                json.dumps(event.get("start_time"), sort_keys=True),
            )
            if key in seen:
                continue
            seen.add(key)
            merged.append(event)
    if metadata is not None:
        metadata["event_count"] = len(merged)
    return merged


def run_user_name(
    executable: str = "lark-cli",
    timeout: float = 30.0,
    runner: Callable[..., subprocess.CompletedProcess] = subprocess.run,
) -> str:
    return parse_user_name(_run(identity_command(executable), timeout, runner))
