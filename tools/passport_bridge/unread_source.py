"""Read the macOS Feishu Dock badge without accessing message content."""

from __future__ import annotations

import re
import subprocess
from typing import Callable, List


MAX_COUNTER = 65535
_STATUS_LABEL = re.compile(
    r"""["']?StatusLabel["']?\s*=\s*(?:\{\s*["']?label["']?\s*=\s*)?"""
    r"""(?:["']([^"']*)["']|([^,;}\s]+))"""
)


class UnreadSourceError(RuntimeError):
    """A sanitized unread-source failure safe to show in bridge logs."""


def unread_command(executable: str = "lsappinfo") -> List[str]:
    """Return the fixed argv used to read only the Feishu Dock badge."""
    return [
        executable,
        "info",
        "-only",
        "StatusLabel",
        "com.electron.lark",
    ]


def _failure(status: str) -> UnreadSourceError:
    return UnreadSourceError(f"Feishu unread badge query failed (status: {status})")


def parse_unread_badge(raw_output: str) -> int:
    """Parse a numeric StatusLabel, treating a missing or empty badge as zero."""
    if not isinstance(raw_output, str):
        raise _failure("invalid-response")
    match = _STATUS_LABEL.search(raw_output)
    if match is None:
        return 0
    label = (match.group(1) if match.group(1) is not None else match.group(2)).strip()
    if not label:
        return 0
    if not label.isdecimal():
        raise _failure("invalid-label")
    return min(int(label), MAX_COUNTER)


def run_unread_count(
    executable: str = "lsappinfo",
    timeout: float = 10.0,
    runner: Callable[..., subprocess.CompletedProcess] = subprocess.run,
) -> int:
    """Run lsappinfo without a shell and return only the bounded badge count."""
    command = unread_command(executable)
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
    return parse_unread_badge(result.stdout)
