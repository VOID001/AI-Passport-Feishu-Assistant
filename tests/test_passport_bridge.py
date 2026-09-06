#!/usr/bin/env python3
"""Focused host tests for the macOS passport BLE bridge."""

from __future__ import annotations

import base64
import io
import json
import subprocess
import unittest
import zlib
from contextlib import redirect_stdout
from datetime import datetime, timedelta, timezone
from unittest.mock import patch

from PIL import Image

from tools.passport_bridge.calendar_source import (
    CalendarSourceError,
    agenda_command,
    calendar_list_command,
    parse_agenda_output,
    parse_owner_calendar_ids,
    run_agenda,
)
from tools.passport_bridge.cli import build_parser, run as run_cli, synchronize_once
from tools.passport_bridge.profile_source import (
    AVATAR_RGB565_BYTES,
    Profile,
    ProfileSourceError,
    avatar_bytes_to_rgb565,
    fetch_avatar_rgb565,
    parse_profile_output,
    profile_command,
    run_profile,
)
from tools.passport_bridge.protocol import (
    BEGIN,
    BEGIN_STRUCT,
    COMMIT,
    DATA,
    DATA_HEADER_STRUCT,
    MAX_EVENTS,
    MAX_EVENT_TITLE_BYTES,
    MAX_PAYLOAD_BYTES,
    MAX_TASKS,
    MAX_TASK_TITLE_BYTES,
    PROTOCOL_VERSION,
    STATUS_STRUCT,
    SUMMARY_FIELDS,
    FrameResult,
    ProtocolError,
    build_transfer,
    encode_summary,
    parse_status,
    verify_commit,
)
from tools.passport_bridge.summary import (
    MAX_USER_NAME_BYTES,
    build_summary,
    truncate_utf8,
)
from tools.passport_bridge.task_source import (
    TaskSourceError,
    parse_recent_tasks,
    run_recent_tasks,
    task_command,
)
from tools.passport_bridge.transport import BleTransportError, PassportBleTransport
from tools.passport_bridge.unread_source import (
    UnreadSourceError,
    parse_unread_badge,
    run_unread_count,
    unread_command,
)


def time_info(value: str) -> dict:
    return {"datetime": value, "timezone": "Asia/Shanghai"}


def event(title: str, start: str, end: str, **extra: object) -> dict:
    item = {
        "summary": title,
        "start_time": time_info(start),
        "end_time": time_info(end),
    }
    item.update(extra)
    return item


def task(title: str, due: datetime, *, all_day: bool = False, **extra: object) -> dict:
    item = {
        "guid": f"task-{abs(hash(title))}",
        "summary": title,
        "due": {
            "timestamp": str(int(due.timestamp() * 1000)),
            "is_all_day": all_day,
        },
        "status": "todo",
    }
    item.update(extra)
    return item


def png_bytes(size: tuple[int, int], color: tuple[int, int, int]) -> bytes:
    output = io.BytesIO()
    Image.new("RGB", size, color).save(output, format="PNG")
    return output.getvalue()


class MemoryResponse(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


class CalendarSourceTest(unittest.TestCase):
    def test_parses_lark_cli_envelope_and_empty_agenda(self) -> None:
        raw = json.dumps(
            {
                "ok": True,
                "data": [
                    event(
                        "Planning",
                        "2026-09-03T09:00:00+08:00",
                        "2026-09-03T10:00:00+08:00",
                    )
                ],
            }
        )
        self.assertEqual(parse_agenda_output(raw)[0]["summary"], "Planning")
        self.assertEqual(parse_agenda_output('{"ok":true,"data":[]}'), [])

    def test_invokes_owner_calendar_commands_without_credentials_or_shell(self) -> None:
        captured = []

        def fake_runner(argv, **kwargs):
            captured.append((argv, kwargs))
            if argv[1:4] == ["calendar", "calendars", "list"]:
                body = {
                    "ok": True,
                    "data": {
                        "calendar_list": [
                            {"calendar_id": "owned", "role": "owner"},
                            {"calendar_id": "subscribed", "role": "reader"},
                        ]
                    },
                }
                return subprocess.CompletedProcess(argv, 0, json.dumps(body), "")
            return subprocess.CompletedProcess(argv, 0, '{"ok":true,"data":[]}', "")

        metadata = {}
        self.assertEqual(run_agenda(runner=fake_runner, metadata=metadata), [])
        self.assertEqual(
            metadata,
            {"owner_calendar_count": 1, "event_count": 0},
        )
        self.assertEqual(captured[0][0], calendar_list_command())
        self.assertEqual(captured[1][0], agenda_command("lark-cli", "owned"))
        joined = " ".join(part for argv, _ in captured for part in argv).lower()
        self.assertNotIn("token", joined)
        self.assertNotIn("secret", joined)
        self.assertTrue(all("shell" not in kwargs for _, kwargs in captured))

    def test_calendar_failure_does_not_echo_process_output(self) -> None:
        leaked = "access_token=very-sensitive app_secret=also-sensitive"

        def fake_runner(argv, **kwargs):
            return subprocess.CompletedProcess(argv, 1, leaked, leaked)

        with self.assertRaises(CalendarSourceError) as caught:
            run_agenda(runner=fake_runner)
        self.assertNotIn("very-sensitive", str(caught.exception))
        self.assertNotIn("also-sensitive", str(caught.exception))

    def test_filters_calendar_list_to_owner_only(self) -> None:
        raw = json.dumps(
            {
                "ok": True,
                "data": {
                    "calendar_list": [
                        {"calendar_id": "primary", "role": "owner"},
                        {"calendar_id": "public", "role": "reader"},
                    ]
                },
            }
        )
        self.assertEqual(parse_owner_calendar_ids(raw), ["primary"])


class ProfileSourceTest(unittest.TestCase):
    def test_uses_exact_profile_command_and_parses_fields(self) -> None:
        self.assertEqual(
            profile_command("/custom/lark-cli"),
            [
                "/custom/lark-cli",
                "contact",
                "+get-user",
                "--as",
                "user",
                "--json",
            ],
        )
        name, avatar = parse_profile_output(
            json.dumps(
                {
                    "ok": True,
                    "data": {
                        "name": " 张剑秋 ",
                        "avatar_thumb": "https://example.invalid/avatar.png",
                    },
                }
            )
        )
        self.assertEqual(name, "张剑秋")
        self.assertEqual(avatar, "https://example.invalid/avatar.png")

    def test_converts_32x32_little_endian_rgb565(self) -> None:
        encoded = avatar_bytes_to_rgb565(png_bytes((40, 30), (255, 0, 0)))
        pixels = base64.b64decode(encoded, validate=True)
        self.assertEqual(len(pixels), AVATAR_RGB565_BYTES)
        self.assertEqual(pixels, b"\x00\xf8" * (32 * 32))

    def test_avatar_failure_uses_empty_fallback(self) -> None:
        self.assertEqual(fetch_avatar_rgb565(""), "")

        def opener(*args, **kwargs):
            return MemoryResponse(b"not an image")

        self.assertEqual(
            fetch_avatar_rgb565("https://private.invalid/avatar", opener=opener),
            "",
        )

    def test_profile_command_has_no_credentials_and_errors_are_sanitized(self) -> None:
        leaked = "https://avatar.invalid/private access_token=secret"
        captured = []

        def fake_runner(argv, **kwargs):
            captured.append((argv, kwargs))
            return subprocess.CompletedProcess(argv, 1, leaked, leaked)

        with self.assertRaises(ProfileSourceError) as caught:
            run_profile(runner=fake_runner)
        self.assertEqual(captured[0][0], profile_command())
        self.assertNotIn("shell", captured[0][1])
        self.assertNotIn("avatar.invalid", str(caught.exception))
        self.assertNotIn("secret", str(caught.exception))


class TaskSourceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.now = datetime(2026, 9, 4, 12, 0, tzinfo=timezone.utc)

    def test_uses_exact_task_command_without_shell(self) -> None:
        self.assertEqual(
            task_command(),
            [
                "lark-cli",
                "task",
                "+get-my-tasks",
                "--as",
                "user",
                "--complete=false",
                "--due-start=-30d",
                "--due-end=+1d",
                "--page-all",
                "--json",
            ],
        )
        captured = []

        def fake_runner(argv, **kwargs):
            captured.append((argv, kwargs))
            return subprocess.CompletedProcess(
                argv, 0, '{"ok":true,"data":{"items":[]}}', ""
            )

        self.assertEqual(run_recent_tasks(self.now, runner=fake_runner), [])
        self.assertEqual(captured[0][0], task_command())
        self.assertNotIn("shell", captured[0][1])

    def test_filters_incomplete_tasks_to_thirty_local_dates(self) -> None:
        items = [
            {"summary": "old edge", "due_at": "2026-08-06T00:00:00+00:00"},
            {"summary": "too old", "due_at": "2026-08-05T23:59:59+00:00"},
            {"summary": "today", "due_at": "2026-09-04T23:59:59+00:00"},
            {"summary": "future", "due_at": "2026-09-05T00:00:00+00:00"},
            {
                "summary": "native shape",
                "due": {
                    "timestamp": str(int((self.now - timedelta(days=2)).timestamp() * 1000)),
                    "is_all_day": True,
                },
            },
            {
                "summary": "done",
                "status": "done",
                "due_at": (self.now - timedelta(days=1)).isoformat(),
            },
            {"summary": "no due"},
        ]
        raw = json.dumps({"ok": True, "data": {"items": items}})
        self.assertEqual(
            [item["summary"] for item in parse_recent_tasks(raw, self.now)],
            ["old edge", "today", "native shape"],
        )

    def test_task_failure_does_not_echo_process_output(self) -> None:
        leaked = "task title and access_token=private"

        def fake_runner(argv, **kwargs):
            return subprocess.CompletedProcess(argv, 2, leaked, leaked)

        with self.assertRaises(TaskSourceError) as caught:
            run_recent_tasks(self.now, runner=fake_runner)
        self.assertNotIn("task title", str(caught.exception))
        self.assertNotIn("private", str(caught.exception))


class UnreadSourceTest(unittest.TestCase):
    def test_uses_exact_lsappinfo_argv_without_shell(self) -> None:
        self.assertEqual(
            unread_command(),
            [
                "lsappinfo",
                "info",
                "-only",
                "StatusLabel",
                "com.electron.lark",
            ],
        )
        captured = []

        def fake_runner(argv, **kwargs):
            captured.append((argv, kwargs))
            return subprocess.CompletedProcess(argv, 0, '"StatusLabel"="4096"', "")

        self.assertEqual(run_unread_count(runner=fake_runner), 4096)
        self.assertEqual(captured[0][0], unread_command())
        self.assertNotIn("shell", captured[0][1])

    def test_parses_empty_missing_large_and_overflow_badges(self) -> None:
        self.assertEqual(
            parse_unread_badge('"StatusLabel"={ "label"="911" }'),
            911,
        )
        self.assertEqual(parse_unread_badge('StatusLabel = "300";'), 300)
        self.assertEqual(parse_unread_badge('StatusLabel = "";'), 0)
        self.assertEqual(parse_unread_badge("{}"), 0)
        self.assertEqual(parse_unread_badge('StatusLabel = "99999";'), 65535)

    def test_rejects_non_numeric_badge_and_sanitizes_process_failure(self) -> None:
        with self.assertRaises(UnreadSourceError):
            parse_unread_badge('StatusLabel = "2 new";')

        def fake_runner(argv, **kwargs):
            return subprocess.CompletedProcess(argv, 1, "private chat", "secret")

        with self.assertRaises(UnreadSourceError) as caught:
            run_unread_count(runner=fake_runner)
        self.assertNotIn("private chat", str(caught.exception))
        self.assertNotIn("secret", str(caught.exception))


class SummaryConversionTest(unittest.TestCase):
    def setUp(self) -> None:
        self.now = datetime.fromisoformat("2026-09-03T10:30:00+08:00")

    def test_builds_time_and_count_fields(self) -> None:
        summary = build_summary(
            [
                event(
                    "已结束",
                    "2026-09-03T09:00:00+08:00",
                    "2026-09-03T10:00:00+08:00",
                ),
                event(
                    "未来会议",
                    "2026-09-03T11:00:00+08:00",
                    "2026-09-03T12:00:00+08:00",
                ),
            ],
            "张剑秋",
            self.now,
            overdue_task_count=65535,
            unread_message_count=300,
        )
        self.assertEqual(summary["version"], 4)
        self.assertEqual(summary["generated_at"], "2026-09-03T02:30:00Z")
        self.assertEqual(summary["generated_at_epoch"], int(self.now.timestamp()))
        self.assertEqual(summary["utc_offset_minutes"], 480)
        self.assertEqual(summary["user_name"], "张剑秋")
        self.assertEqual(summary["overdue_task_count"], 65535)
        self.assertEqual(summary["unread_message_count"], 300)
        self.assertTrue(summary["events"][0]["completed"])
        self.assertFalse(summary["events"][1]["completed"])

    def test_filters_sorts_truncates_and_limits_recent_tasks(self) -> None:
        tasks = [
            task("older", self.now - timedelta(days=29), all_day=True),
            task("today", self.now),
            task("too old", self.now - timedelta(days=30)),
            task("future", self.now + timedelta(days=1)),
            task("done", self.now - timedelta(days=1), status="done"),
        ]
        tasks.extend(
            task("中" * 80 + str(index), self.now - timedelta(days=index))
            for index in range(1, 18)
        )
        summary = build_summary([], "User", self.now, tasks=tasks)
        self.assertEqual(len(summary["tasks"]), 16)
        self.assertEqual(summary["tasks"][0]["title"], "today")
        self.assertEqual(summary["overdue_task_count"], 19)
        self.assertNotIn("too old", [item["title"] for item in summary["tasks"]])
        self.assertNotIn("future", [item["title"] for item in summary["tasks"]])
        self.assertNotIn("done", [item["title"] for item in summary["tasks"]])
        self.assertLessEqual(
            len(summary["tasks"][1]["title"].encode("utf-8")),
            MAX_TASK_TITLE_BYTES,
        )

    def test_preserves_and_safely_truncates_utf8(self) -> None:
        value = "  中文   日程标题很长  "
        truncated = truncate_utf8(value, 13, "Busy")
        self.assertEqual(truncated, "中文 日程")
        self.assertLessEqual(len(truncated.encode("utf-8")), 13)
        self.assertFalse(truncated.isascii())

        summary = build_summary(
            [
                event(
                    "超长会议标题" * 20,
                    "2026-09-03T09:00:00+08:00",
                    "2026-09-03T10:00:00+08:00",
                )
            ],
            "用户名" * 20,
            self.now,
        )
        self.assertLessEqual(
            len(summary["events"][0]["title"].encode("utf-8")),
            MAX_EVENT_TITLE_BYTES,
        )
        self.assertLessEqual(
            len(summary["user_name"].encode("utf-8")), MAX_USER_NAME_BYTES
        )
        self.assertIn("用", summary["user_name"])
        self.assertIn("超", summary["events"][0]["title"])

    def test_truncates_titles_at_utf8_byte_boundaries_without_splitting(self) -> None:
        event_boundary = "中" * 85
        task_boundary = "中" * 42 + "a"
        summary = build_summary(
            [
                event(
                    event_boundary + "a",
                    "2026-09-03T09:00:00+08:00",
                    "2026-09-03T10:00:00+08:00",
                )
            ],
            "User",
            self.now,
            tasks=[task(task_boundary + "a", self.now)],
        )

        self.assertEqual(summary["events"][0]["title"], event_boundary)
        self.assertEqual(
            len(summary["events"][0]["title"].encode("utf-8")),
            MAX_EVENT_TITLE_BYTES,
        )
        self.assertEqual(summary["tasks"][0]["title"], task_boundary)
        self.assertEqual(
            len(summary["tasks"][0]["title"].encode("utf-8")),
            MAX_TASK_TITLE_BYTES,
        )

    def test_preserves_full_review_meeting_title(self) -> None:
        title = (
            "TokaDB评审会(包含方案评审/串讲/业务讨论等等) / "
            "TokaDB Review Meeting (including proposal review, "
            "presentation, business discussion, etc.)"
        )
        summary = build_summary(
            [
                event(
                    title,
                    "2026-09-03T09:00:00+08:00",
                    "2026-09-03T10:00:00+08:00",
                )
            ],
            "张剑秋",
            self.now,
        )
        self.assertEqual(summary["events"][0]["title"], title)

    def test_all_day_empty_filter_sort_and_limit_behavior(self) -> None:
        all_day = {
            "summary": "全天",
            "start_time": {"date": "2026-09-03", "timezone": "Asia/Shanghai"},
            "end_time": {"date": "2026-09-03", "timezone": "Asia/Shanghai"},
        }
        events = [all_day]
        for index in range(18):
            events.append(
                event(
                    f"Event {index:02d}",
                    f"2026-09-03T{index:02d}:00:00+08:00",
                    f"2026-09-03T{index:02d}:30:00+08:00",
                )
            )
        events.append(
            event(
                "Tomorrow",
                "2026-09-04T09:00:00+08:00",
                "2026-09-04T10:00:00+08:00",
            )
        )
        summary = build_summary(reversed(events), "User", self.now)
        self.assertEqual(len(summary["events"]), 16)
        self.assertTrue(summary["events"][0]["all_day"])
        self.assertNotIn("Tomorrow", [item["title"] for item in summary["events"]])
        self.assertEqual(build_summary([], "User", self.now)["events"], [])


class ProtocolTest(unittest.TestCase):
    def setUp(self) -> None:
        self.summary = build_summary(
            [], "User", datetime(2026, 9, 3, tzinfo=timezone.utc)
        )

    def test_encodes_exact_v4_schema_and_rejects_sensitive_fields(self) -> None:
        payload = encode_summary(self.summary)
        decoded = json.loads(payload)
        self.assertEqual(frozenset(decoded), SUMMARY_FIELDS)
        self.assertEqual(
            list(decoded),
            sorted(
                [
                    "version",
                    "generated_at",
                    "generated_at_epoch",
                    "utc_offset_minutes",
                    "user_name",
                    "avatar_rgb565",
                    "date",
                    "events",
                    "tasks",
                    "overdue_task_count",
                    "unread_message_count",
                ]
            ),
        )
        self.assertEqual(decoded["version"], PROTOCOL_VERSION)
        for forbidden in (
            b"access_token",
            b"app_secret",
            b"avatar_thumb",
            b"https://",
            b"task_title",
            b"message_body",
        ):
            self.assertNotIn(forbidden, payload)

        invalid = dict(self.summary)
        invalid["access_token"] = "must-not-cross-ble"
        with self.assertRaisesRegex(ProtocolError, "approved schema"):
            encode_summary(invalid)

    def test_validates_avatar_counters_epoch_and_offset(self) -> None:
        avatar = base64.b64encode(b"\x34\x12" * (32 * 32)).decode("ascii")
        valid = dict(self.summary)
        valid.update(
            avatar_rgb565=avatar,
            overdue_task_count=65535,
            unread_message_count=300,
            generated_at_epoch=0xFFFFFFFF,
            utc_offset_minutes=-840,
        )
        self.assertLessEqual(len(encode_summary(valid)), MAX_PAYLOAD_BYTES)

        invalid_cases = (
            ("avatar_rgb565", "https://private.invalid/avatar"),
            ("avatar_rgb565", base64.b64encode(b"short").decode("ascii")),
            ("overdue_task_count", 65536),
            ("unread_message_count", -1),
            ("generated_at_epoch", 0x100000000),
            ("utc_offset_minutes", 841),
        )
        for field, value in invalid_cases:
            with self.subTest(field=field, value=value):
                invalid = dict(self.summary)
                invalid[field] = value
                with self.assertRaises(ProtocolError):
                    encode_summary(invalid)

        invalid_task = dict(self.summary)
        invalid_task["tasks"] = [
            {"guid": "task-1", "title": "Task", "due_at_epoch": -1, "all_day": False}
        ]
        with self.assertRaises(ProtocolError):
            encode_summary(invalid_task)

    def test_enforces_event_and_task_title_utf8_byte_boundaries(self) -> None:
        event_boundary = "中" * 85
        task_boundary = "中" * 42 + "a"
        valid = dict(self.summary)
        valid["events"] = [
            {
                "title": event_boundary,
                "start_minute": 0,
                "end_minute": 1,
                "all_day": False,
                "completed": False,
            }
        ]
        valid["tasks"] = [
            {
                "guid": "task-boundary",
                "title": task_boundary,
                "due_at_epoch": 0,
                "all_day": False,
            }
        ]
        decoded = json.loads(encode_summary(valid))
        self.assertEqual(decoded["events"][0]["title"], event_boundary)
        self.assertEqual(decoded["tasks"][0]["title"], task_boundary)

        invalid_event = dict(valid)
        invalid_event["events"] = [
            {**valid["events"][0], "title": event_boundary + "a"}
        ]
        with self.assertRaisesRegex(ProtocolError, "255 UTF-8 bytes"):
            encode_summary(invalid_event)

        invalid_task = dict(valid)
        invalid_task["tasks"] = [
            {**valid["tasks"][0], "title": task_boundary + "a"}
        ]
        with self.assertRaisesRegex(ProtocolError, "127 UTF-8 bytes"):
            encode_summary(invalid_task)

    def test_enforces_event_and_task_count_boundaries(self) -> None:
        event_item = {
            "title": "Event",
            "start_minute": 0,
            "end_minute": 1,
            "all_day": False,
            "completed": False,
        }
        task_item = {
            "guid": "task-boundary",
            "title": "Task",
            "due_at_epoch": 0,
            "all_day": False,
        }
        valid = dict(self.summary)
        valid["events"] = [dict(event_item) for _ in range(MAX_EVENTS)]
        valid["tasks"] = [
            {**task_item, "guid": f"task-{index}"}
            for index in range(MAX_TASKS)
        ]
        encode_summary(valid)

        invalid_events = dict(valid)
        invalid_events["events"] = [
            dict(event_item) for _ in range(MAX_EVENTS + 1)
        ]
        with self.assertRaisesRegex(ProtocolError, "at most 16 items"):
            encode_summary(invalid_events)

        invalid_tasks = dict(valid)
        invalid_tasks["tasks"] = [
            {**task_item, "guid": f"task-{index}"}
            for index in range(MAX_TASKS + 1)
        ]
        with self.assertRaisesRegex(ProtocolError, "at most 16 items"):
            encode_summary(invalid_tasks)

    def test_worst_case_16_event_avatar_payload_fits(self) -> None:
        current = datetime.fromisoformat("2026-09-03T16:30:00+08:00")
        events = []
        for index in range(16):
            events.append(
                event(
                    "中" * 85,
                    f"2026-09-03T{index:02d}:00:00+08:00",
                    f"2026-09-03T{index:02d}:59:00+08:00",
                )
            )
        avatar = base64.b64encode(b"\xff" * AVATAR_RGB565_BYTES).decode("ascii")
        tasks = [
            task("中" * 42, current - timedelta(days=index % 29))
            for index in range(16)
        ]
        summary = build_summary(
            events,
            "用户名" * 20,
            current,
            tasks=tasks,
            avatar_rgb565=avatar,
            overdue_task_count=65535,
            unread_message_count=65535,
        )
        payload = encode_summary(summary)
        self.assertEqual(len(summary["events"]), 16)
        self.assertEqual(len(summary["tasks"]), 16)
        self.assertEqual(len(base64.b64decode(summary["avatar_rgb565"])), 2048)
        self.assertLessEqual(len(payload), MAX_PAYLOAD_BYTES)

    def test_builds_begin_sequenced_data_and_commit_frames(self) -> None:
        payload = encode_summary(self.summary)
        transfer = build_transfer(payload, chunk_bytes=37)
        expected_crc = zlib.crc32(payload) & 0xFFFFFFFF
        opcode, length, checksum = BEGIN_STRUCT.unpack(transfer.frames[0])
        self.assertEqual(opcode, BEGIN)
        self.assertEqual((length, checksum), (len(payload), expected_crc))

        rebuilt = bytearray()
        for sequence, frame in enumerate(transfer.frames[1:-1]):
            opcode, parsed_sequence = DATA_HEADER_STRUCT.unpack(
                frame[: DATA_HEADER_STRUCT.size]
            )
            self.assertEqual(opcode, DATA)
            self.assertEqual(parsed_sequence, sequence)
            rebuilt.extend(frame[DATA_HEADER_STRUCT.size :])
        self.assertEqual(bytes(rebuilt), payload)
        self.assertEqual(transfer.frames[-1], bytes((COMMIT,)))

    def test_rejects_oversized_payload_and_verifies_commit(self) -> None:
        self.assertEqual(
            len(build_transfer(b"x" * MAX_PAYLOAD_BYTES).payload),
            MAX_PAYLOAD_BYTES,
        )
        with self.assertRaisesRegex(ProtocolError, str(MAX_PAYLOAD_BYTES)):
            build_transfer(b"x" * (MAX_PAYLOAD_BYTES + 1))
        verify_commit(parse_status(STATUS_STRUCT.pack(FrameResult.COMPLETE)))
        with self.assertRaisesRegex(
            ProtocolError, "did not commit the transfer: BAD_CRC"
        ):
            verify_commit(parse_status(STATUS_STRUCT.pack(FrameResult.BAD_CRC)))


class TransportProgressTest(unittest.IsolatedAsyncioTestCase):
    async def test_preserves_device_rejection_status_in_error(self) -> None:
        class FakeScanner:
            @staticmethod
            async def find_device_by_filter(matcher, timeout):
                del matcher, timeout
                return object()

        class FakeClient:
            is_connected = True

            def __init__(self, device, timeout):
                del device, timeout
                self.callback = None

            async def __aenter__(self):
                return self

            async def __aexit__(self, *args):
                del args

            async def start_notify(self, characteristic, callback):
                del characteristic
                self.callback = callback

            async def read_gatt_char(self, characteristic):
                if characteristic.endswith("28f-f7bd-485a-bd9d-92ad6ecfe93e"):
                    return bytes((4,))
                return STATUS_STRUCT.pack(FrameResult.ACCEPTED)

            async def stop_notify(self, characteristic):
                del characteristic

            async def write_gatt_char(self, characteristic, frame, response):
                del characteristic, response
                if frame == bytes((COMMIT,)):
                    self.callback(
                        None,
                        bytearray(STATUS_STRUCT.pack(FrameResult.BAD_FORMAT)),
                    )

        with patch(
            "tools.passport_bridge.transport._load_bleak",
            return_value=(FakeClient, FakeScanner),
        ):
            with self.assertRaisesRegex(BleTransportError, "BAD_FORMAT"):
                await PassportBleTransport().send(build_transfer(b"payload"))

    async def test_reports_discovery_connection_frames_and_ack(self) -> None:
        progress = []

        class FakeScanner:
            @staticmethod
            async def find_device_by_filter(matcher, timeout):
                del matcher, timeout
                return object()

        class FakeClient:
            def __init__(self, device, timeout):
                del device, timeout
                self.callback = None

            async def __aenter__(self):
                return self

            async def __aexit__(self, *args):
                del args

            async def start_notify(self, characteristic, callback):
                del characteristic
                self.callback = callback

            async def read_gatt_char(self, characteristic):
                if characteristic.endswith("28f-f7bd-485a-bd9d-92ad6ecfe93e"):
                    return bytes((4,))
                return STATUS_STRUCT.pack(FrameResult.ACCEPTED)

            async def stop_notify(self, characteristic):
                del characteristic

            async def write_gatt_char(self, characteristic, frame, response):
                del characteristic, response
                if frame == bytes((COMMIT,)):
                    self.callback(
                        None,
                        bytearray(
                            STATUS_STRUCT.pack(FrameResult.COMPLETE)
                        ),
                    )

        transfer = build_transfer(b"payload", chunk_bytes=3)
        transport = PassportBleTransport(
            scan_timeout=2,
            connect_timeout=3,
            acknowledge_timeout=4,
        )
        with patch(
            "tools.passport_bridge.transport._load_bleak",
            return_value=(FakeClient, FakeScanner),
        ):
            await transport.send(
                transfer,
                progress=lambda phase, **details: progress.append(
                    (phase, details)
                ),
            )

        events = [details["event"] for _, details in progress]
        self.assertEqual(
            events,
            [
                "scan_started",
                "device_found",
                "connected",
                "protocol_version_checked",
                "notifications_ready",
                "transfer_started",
                "begin_sent",
                "chunk_sent",
                "chunk_sent",
                "chunk_sent",
                "commit_sent",
                "ack_received",
            ],
        )
        chunks = [
            details
            for _, details in progress
            if details["event"] == "chunk_sent"
        ]
        self.assertEqual(
            [details["frame_index"] for details in chunks],
            [1, 2, 3],
        )
        self.assertEqual(
            [details["data_bytes"] for details in chunks],
            [3, 3, 1],
        )
        self.assertEqual(progress[-1][1]["result"], "COMPLETE")

    async def test_missing_completion_queue_does_not_block_summary_transfer(self) -> None:
        progress = []
        missing_characteristic = type(
            "BleakCharacteristicNotFoundError", (Exception,), {}
        )

        class FakeScanner:
            @staticmethod
            async def find_device_by_filter(matcher, timeout):
                del matcher, timeout
                return object()

        class FakeClient:
            def __init__(self, device, timeout):
                del device, timeout
                self.callback = None

            async def __aenter__(self):
                return self

            async def __aexit__(self, *args):
                del args

            async def read_gatt_char(self, characteristic):
                if characteristic.endswith("28d-f7bd-485a-bd9d-92ad6ecfe93e"):
                    raise missing_characteristic(
                        f"Characteristic {characteristic} was not found!"
                    )
                if characteristic.endswith("28f-f7bd-485a-bd9d-92ad6ecfe93e"):
                    return bytes((4,))
                return STATUS_STRUCT.pack(FrameResult.ACCEPTED)

            async def start_notify(self, characteristic, callback):
                del characteristic
                self.callback = callback

            async def stop_notify(self, characteristic):
                del characteristic

            async def write_gatt_char(self, characteristic, frame, response):
                del characteristic, response
                if frame == bytes((COMMIT,)):
                    self.callback(
                        None, bytearray(STATUS_STRUCT.pack(FrameResult.COMPLETE))
                    )

        with patch(
            "tools.passport_bridge.transport._load_bleak",
            return_value=(FakeClient, FakeScanner),
        ):
            await PassportBleTransport().send(
                build_transfer(b"payload"),
                progress=lambda phase, **details: progress.append(
                    (phase, details)
                ),
                completion_handler=lambda guid: True,
            )

        self.assertIn(
            "completion_queue_unavailable",
            [details["event"] for _, details in progress],
        )
        self.assertEqual(progress[-1][1]["result"], "COMPLETE")

    async def test_rejects_mismatched_device_protocol_before_transfer(self) -> None:
        class FakeScanner:
            @staticmethod
            async def find_device_by_filter(matcher, timeout):
                del matcher, timeout
                return object()

        class FakeClient:
            def __init__(self, device, timeout):
                del device, timeout
                self.writes = []

            async def __aenter__(self):
                return self

            async def __aexit__(self, *args):
                del args

            async def read_gatt_char(self, characteristic):
                if characteristic.endswith("28f-f7bd-485a-bd9d-92ad6ecfe93e"):
                    return bytes((3,))
                return STATUS_STRUCT.pack(FrameResult.ACCEPTED)

            async def write_gatt_char(self, characteristic, frame, response):
                del characteristic, response
                self.writes.append(frame)

        with patch(
            "tools.passport_bridge.transport._load_bleak",
            return_value=(FakeClient, FakeScanner),
        ):
            with self.assertRaisesRegex(
                BleTransportError, "host v4, device v3"
            ):
                await PassportBleTransport().send(build_transfer(b"payload"))


class CliModeTest(unittest.IsolatedAsyncioTestCase):
    def test_supports_one_shot_and_interval_modes(self) -> None:
        parser = build_parser()
        one_shot = parser.parse_args([])
        override = parser.parse_args(["--user-name", "User"])
        interval = parser.parse_args(["--user-name", "User", "--interval", "300"])
        self.assertIsNone(one_shot.interval)
        self.assertIsNone(one_shot.user_name)
        self.assertEqual(override.user_name, "User")
        self.assertEqual(interval.interval, 300.0)

    async def test_json_output_is_machine_readable(self) -> None:
        args = build_parser().parse_args(["--json-output"])
        summary = {
            "version": 3,
            "user_name": "张剑秋",
            "events": [],
            "tasks": [],
            "overdue_task_count": 1,
            "unread_message_count": 2,
        }
        output = io.StringIO()
        with (
            patch(
                "tools.passport_bridge.cli.synchronize_once",
                return_value=(summary, 321),
            ),
            redirect_stdout(output),
        ):
            self.assertEqual(await run_cli(args), 0)
        result = json.loads(output.getvalue())
        self.assertTrue(result["ok"])
        self.assertEqual(result["payload_bytes"], 321)
        self.assertEqual(result["summary"]["user_name"], "张剑秋")
        self.assertNotIn("token", output.getvalue().lower())

    async def test_collects_profile_tasks_unread_and_builds_summary(self) -> None:
        args = build_parser().parse_args(["--user-name", "Override"])
        sent = []

        class FakeTransport:
            def __init__(self, **kwargs):
                self.kwargs = kwargs

            async def send(self, transfer, progress=None, completion_handler=None):
                del completion_handler
                if progress:
                    progress("connecting")
                    progress("transferring")
                    progress("complete")
                sent.append(transfer)

        avatar = base64.b64encode(b"\x00" * AVATAR_RGB565_BYTES).decode("ascii")
        output = io.StringIO()
        with (
            patch("tools.passport_bridge.cli.run_agenda", return_value=[]),
            patch(
                "tools.passport_bridge.cli.run_profile",
                return_value=Profile("Profile Name", avatar),
            ),
            patch(
                "tools.passport_bridge.cli.run_recent_tasks",
                return_value=[
                    {
                        "guid": "task-review-proposal",
                        "summary": "Review proposal",
                        "due_at": datetime.now().astimezone().isoformat(),
                    }
                ],
            ),
            patch("tools.passport_bridge.cli.run_unread_count", return_value=300),
            patch("tools.passport_bridge.cli.PassportBleTransport", FakeTransport),
            redirect_stdout(output),
        ):
            await synchronize_once(args)

        decoded = json.loads(sent[0].payload)
        self.assertEqual(decoded["user_name"], "Override")
        self.assertEqual(decoded["avatar_rgb565"], avatar)
        self.assertEqual(decoded["overdue_task_count"], 1)
        self.assertEqual(decoded["tasks"][0]["title"], "Review proposal")
        self.assertEqual(decoded["unread_message_count"], 300)
        self.assertNotIn("http", output.getvalue().lower())
        self.assertNotIn("token", output.getvalue().lower())

    async def test_emits_real_progress_json_lines(self) -> None:
        args = build_parser().parse_args(["--progress-json"])
        sent = []

        class FakeTransport:
            def __init__(self, **kwargs):
                del kwargs

            async def send(self, transfer, progress=None, completion_handler=None):
                del completion_handler
                sent.append(transfer)
                progress(
                    "connecting",
                    event="scan_started",
                    device_name="FoloPassport",
                    scan_timeout_seconds=10,
                )
                progress(
                    "transferring",
                    event="transfer_started",
                    payload_bytes=len(transfer.payload),
                    data_frame_count=transfer.data_frame_count,
                )
                progress(
                    "complete",
                    event="ack_received",
                    result="COMPLETE",
                )

        output = io.StringIO()
        with (
            patch("tools.passport_bridge.cli.run_agenda", return_value=[]),
            patch(
                "tools.passport_bridge.cli.run_profile",
                return_value=Profile("User", ""),
            ),
            patch("tools.passport_bridge.cli.run_recent_tasks", return_value=[]),
            patch("tools.passport_bridge.cli.run_unread_count", return_value=0),
            patch("tools.passport_bridge.cli.PassportBleTransport", FakeTransport),
            redirect_stdout(output),
        ):
            await synchronize_once(args)

        documents = [
            json.loads(line) for line in output.getvalue().splitlines()
        ]
        self.assertEqual(
            [document["event"] for document in documents],
            [
                "sources_started",
                "calendar_ready",
                "profile_ready",
                "tasks_ready",
                "unread_ready",
                "summary_ready",
                "payload_ready",
                "scan_started",
                "transfer_started",
                "ack_received",
            ],
        )
        payload_ready = documents[6]["details"]
        self.assertEqual(payload_ready["protocol_version"], 4)
        self.assertGreater(payload_ready["payload_bytes"], 0)
        self.assertGreater(payload_ready["data_frame_count"], 0)
        self.assertEqual(documents[-1]["details"]["result"], "COMPLETE")
        serialized = output.getvalue().lower()
        self.assertNotIn("token", serialized)
        self.assertNotIn("secret", serialized)
        self.assertNotIn("http", serialized)


if __name__ == "__main__":
    unittest.main()
