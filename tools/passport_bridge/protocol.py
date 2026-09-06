"""Binary framing for bounded work-summary transfers over BLE GATT."""

from __future__ import annotations

import base64
import binascii
import json
import struct
import zlib
from dataclasses import dataclass
from enum import IntEnum
from typing import Any, Dict, List, Sequence


DEVICE_NAME = "FoloPassport"
SERVICE_UUID = "7d2ea28a-f7bd-485a-bd9d-92ad6ecfe93e"
WRITE_CHARACTERISTIC_UUID = "7d2ea28b-f7bd-485a-bd9d-92ad6ecfe93e"
STATUS_CHARACTERISTIC_UUID = "7d2ea28c-f7bd-485a-bd9d-92ad6ecfe93e"
COMPLETIONS_CHARACTERISTIC_UUID = "7d2ea28d-f7bd-485a-bd9d-92ad6ecfe93e"
COMPLETIONS_ACK_CHARACTERISTIC_UUID = "7d2ea28e-f7bd-485a-bd9d-92ad6ecfe93e"
PROTOCOL_CHARACTERISTIC_UUID = "7d2ea28f-f7bd-485a-bd9d-92ad6ecfe93e"

PROTOCOL_VERSION = 4
MAX_PAYLOAD_BYTES = 12 * 1024
AVATAR_RGB565_BYTES = 32 * 32 * 2
MAX_EVENTS = 16
MAX_EVENT_TITLE_BYTES = 255
MAX_TASKS = 16
MAX_TASK_TITLE_BYTES = 127
DEFAULT_CHUNK_BYTES = 180
MAX_CHUNK_BYTES = 509

BEGIN = 0x01
DATA = 0x02
COMMIT = 0x03
CANCEL = 0x04

BEGIN_STRUCT = struct.Struct("<BII")
DATA_HEADER_STRUCT = struct.Struct("<BH")
STATUS_STRUCT = struct.Struct("<B")

SUMMARY_FIELDS = frozenset(
    {
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
    }
)
EVENT_FIELDS = frozenset(
    {"title", "start_minute", "end_minute", "all_day", "completed"}
)
TASK_FIELDS = frozenset({"guid", "title", "due_at_epoch", "all_day"})


class ProtocolError(ValueError):
    """A payload or frame violates the bridge protocol."""


class FrameResult(IntEnum):
    ACCEPTED = 0
    COMPLETE = 1
    BAD_FORMAT = 2
    BAD_STATE = 3
    BAD_SEQUENCE = 4
    TOO_LARGE = 5
    BAD_CRC = 6


@dataclass(frozen=True)
class Transfer:
    payload: bytes
    checksum: int
    frames: Sequence[bytes]
    data_frame_count: int


@dataclass(frozen=True)
class DeviceStatus:
    result: FrameResult


def encode_summary(summary: Dict[str, Any]) -> bytes:
    """Serialize only the approved minimal schema to compact UTF-8 JSON."""
    if not isinstance(summary, dict) or frozenset(summary) != SUMMARY_FIELDS:
        raise ProtocolError("summary fields do not match the approved schema")
    if summary["version"] != PROTOCOL_VERSION:
        raise ProtocolError("summary has an unsupported protocol version")
    for field in ("generated_at", "user_name", "date"):
        if not isinstance(summary[field], str) or not summary[field]:
            raise ProtocolError(f"summary {field} must be a non-empty string")
    generated_at_epoch = summary["generated_at_epoch"]
    if (
        not isinstance(generated_at_epoch, int)
        or isinstance(generated_at_epoch, bool)
        or not 0 <= generated_at_epoch <= 0xFFFFFFFF
    ):
        raise ProtocolError("summary generated_at_epoch must be a uint32")
    utc_offset_minutes = summary["utc_offset_minutes"]
    if (
        not isinstance(utc_offset_minutes, int)
        or isinstance(utc_offset_minutes, bool)
        or not -840 <= utc_offset_minutes <= 840
    ):
        raise ProtocolError("summary utc_offset_minutes must be between -840 and 840")

    avatar_rgb565 = summary["avatar_rgb565"]
    if not isinstance(avatar_rgb565, str):
        raise ProtocolError("summary avatar_rgb565 must be a string")
    if avatar_rgb565:
        try:
            avatar_bytes = base64.b64decode(avatar_rgb565, validate=True)
        except (ValueError, binascii.Error) as exc:
            raise ProtocolError("summary avatar_rgb565 is invalid base64") from exc
        if len(avatar_bytes) != AVATAR_RGB565_BYTES:
            raise ProtocolError(
                f"summary avatar_rgb565 must decode to {AVATAR_RGB565_BYTES} bytes"
            )
        if base64.b64encode(avatar_bytes).decode("ascii") != avatar_rgb565:
            raise ProtocolError("summary avatar_rgb565 is not canonical base64")

    for field in ("overdue_task_count", "unread_message_count"):
        value = summary[field]
        if not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= 65535:
            raise ProtocolError(f"summary {field} must be between 0 and 65535")

    events = summary.get("events")
    if not isinstance(events, list) or len(events) > MAX_EVENTS:
        raise ProtocolError(
            f"summary events must be a list of at most {MAX_EVENTS} items"
        )
    for event in events:
        if not isinstance(event, dict) or frozenset(event) != EVENT_FIELDS:
            raise ProtocolError("event fields do not match the approved schema")
        if (
            not isinstance(event["title"], str)
            or not event["title"]
            or len(event["title"].encode("utf-8")) > MAX_EVENT_TITLE_BYTES
        ):
            raise ProtocolError(
                f"event title must be a non-empty string of at most "
                f"{MAX_EVENT_TITLE_BYTES} UTF-8 bytes"
            )
        for field in ("start_minute", "end_minute"):
            value = event[field]
            if (
                not isinstance(value, int)
                or isinstance(value, bool)
                or not 0 <= value <= 24 * 60
            ):
                raise ProtocolError(f"event {field} is outside the valid day")
        if event["end_minute"] < event["start_minute"]:
            raise ProtocolError("event end precedes its start")
        if not isinstance(event["all_day"], bool) or not isinstance(
            event["completed"], bool
        ):
            raise ProtocolError("event flags must be booleans")

    tasks = summary.get("tasks")
    if not isinstance(tasks, list) or len(tasks) > MAX_TASKS:
        raise ProtocolError(
            f"summary tasks must be a list of at most {MAX_TASKS} items"
        )
    for task in tasks:
        if not isinstance(task, dict) or frozenset(task) != TASK_FIELDS:
            raise ProtocolError("task fields do not match the approved schema")
        if (
            not isinstance(task["title"], str)
            or not task["title"]
            or len(task["title"].encode("utf-8")) > MAX_TASK_TITLE_BYTES
        ):
            raise ProtocolError(
                f"task title must be a non-empty string of at most "
                f"{MAX_TASK_TITLE_BYTES} UTF-8 bytes"
            )
        if not isinstance(task["guid"], str) or not task["guid"] or len(task["guid"]) >= 64:
            raise ProtocolError("task guid must be a non-empty bounded string")
        due_at_epoch = task["due_at_epoch"]
        if (
            not isinstance(due_at_epoch, int)
            or isinstance(due_at_epoch, bool)
            or not 0 <= due_at_epoch <= 0xFFFFFFFF
        ):
            raise ProtocolError("task due_at_epoch must be a uint32")
        if not isinstance(task["all_day"], bool):
            raise ProtocolError("task all_day must be a boolean")

    payload = json.dumps(
        summary,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    if len(payload) > MAX_PAYLOAD_BYTES:
        raise ProtocolError(f"summary payload exceeds {MAX_PAYLOAD_BYTES} bytes")
    return payload


def build_transfer(
    payload: bytes, chunk_bytes: int = DEFAULT_CHUNK_BYTES
) -> Transfer:
    """Create begin/data/commit frames with sequence and whole-payload CRC32."""
    if not isinstance(payload, bytes):
        raise ProtocolError("payload must be bytes")
    if not payload:
        raise ProtocolError("payload must not be empty")
    if len(payload) > MAX_PAYLOAD_BYTES:
        raise ProtocolError(f"payload exceeds {MAX_PAYLOAD_BYTES} bytes")
    if not 1 <= chunk_bytes <= MAX_CHUNK_BYTES:
        raise ProtocolError(
            f"chunk size must be between 1 and {MAX_CHUNK_BYTES} bytes"
        )

    checksum = zlib.crc32(payload) & 0xFFFFFFFF
    chunks = [
        payload[offset : offset + chunk_bytes]
        for offset in range(0, len(payload), chunk_bytes)
    ]
    frames: List[bytes] = [
        BEGIN_STRUCT.pack(BEGIN, len(payload), checksum)
    ]
    frames.extend(
        DATA_HEADER_STRUCT.pack(DATA, sequence) + chunk
        for sequence, chunk in enumerate(chunks)
    )
    frames.append(bytes((COMMIT,)))
    return Transfer(payload, checksum, tuple(frames), len(chunks))


def cancel_frame() -> bytes:
    return bytes((CANCEL,))


def parse_status(raw_status: bytes) -> DeviceStatus:
    """Parse the fixed device acknowledgement without accepting trailing data."""
    if len(raw_status) != STATUS_STRUCT.size:
        raise ProtocolError("device status has an invalid length")
    (result,) = STATUS_STRUCT.unpack(raw_status)
    try:
        parsed_result = FrameResult(result)
    except ValueError as exc:
        raise ProtocolError("device status has an unknown frame result") from exc
    return DeviceStatus(parsed_result)


def verify_commit(status: DeviceStatus) -> None:
    """Require the receiver's explicit complete result after commit."""
    if status.result is not FrameResult.COMPLETE:
        raise ProtocolError(
            f"device did not commit the transfer: {status.result.name}"
        )
