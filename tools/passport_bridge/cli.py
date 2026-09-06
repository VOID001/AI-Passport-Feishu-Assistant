"""Command-line entry point for one-shot and interval BLE synchronization."""

from __future__ import annotations

import argparse
import asyncio
import json
import sys
from datetime import datetime
from functools import partial
from typing import Any, Dict, Optional, Sequence, Tuple

from .calendar_source import CalendarSourceError, run_agenda
from .profile_source import AVATAR_RGB565_BYTES, ProfileSourceError, run_profile
from .protocol import (
    DEFAULT_CHUNK_BYTES,
    DEVICE_NAME,
    MAX_PAYLOAD_BYTES,
    PROTOCOL_VERSION,
    SERVICE_UUID,
    STATUS_CHARACTERISTIC_UUID,
    WRITE_CHARACTERISTIC_UUID,
    ProtocolError,
    build_transfer,
    encode_summary,
)
from .summary import SummaryError, build_summary
from .task_source import TaskSourceError, run_recent_tasks
from .transport import BleTransportError, PassportBleTransport
from .unread_source import UnreadSourceError, run_unread_count


class BridgeError(RuntimeError):
    """A bridge failure already safe to print."""


def _positive_float(value: str) -> float:
    parsed = float(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be greater than zero")
    return parsed


def _chunk_size(value: str) -> int:
    parsed = int(value)
    if not 1 <= parsed <= 509:
        raise argparse.ArgumentTypeError("chunk size must be between 1 and 509")
    return parsed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="python3 -m tools.passport_bridge",
        description="Sync Feishu work data to FoloPassport over BLE.",
    )
    parser.add_argument(
        "--user-name",
        help="display name override; defaults to the current lark-cli user",
    )
    parser.add_argument(
        "--interval",
        type=_positive_float,
        help="repeat synchronization at this fixed interval in seconds",
    )
    parser.add_argument(
        "--device",
        help="optional CoreBluetooth device identifier; otherwise scan by name/service",
    )
    parser.add_argument(
        "--json-output",
        action="store_true",
        help="emit one sanitized JSON object per synchronization",
    )
    parser.add_argument(
        "--progress-json",
        action="store_true",
        help="emit machine-readable progress lines before the final result",
    )
    parser.add_argument("--device-name", default=DEVICE_NAME)
    parser.add_argument("--service-uuid", default=SERVICE_UUID)
    parser.add_argument("--write-uuid", default=WRITE_CHARACTERISTIC_UUID)
    parser.add_argument("--status-uuid", default=STATUS_CHARACTERISTIC_UUID)
    parser.add_argument("--chunk-size", type=_chunk_size, default=DEFAULT_CHUNK_BYTES)
    parser.add_argument("--lark-cli", default="lark-cli", dest="lark_cli")
    parser.add_argument("--calendar-timeout", type=_positive_float, default=30.0)
    parser.add_argument("--scan-timeout", type=_positive_float, default=10.0)
    parser.add_argument("--connect-timeout", type=_positive_float, default=15.0)
    parser.add_argument("--ack-timeout", type=_positive_float, default=8.0)
    return parser


async def synchronize_once(
    args: argparse.Namespace,
) -> Tuple[Dict[str, Any], int]:
    loop = asyncio.get_running_loop()
    query_time = datetime.now().astimezone()
    agenda_metadata: Dict[str, int] = {}
    emit_progress(
        args,
        "collecting",
        "sources_started",
        source_count=4,
        date=query_time.date().isoformat(),
    )
    try:
        events, profile, tasks, unread_message_count = (
            await asyncio.gather(
                loop.run_in_executor(
                    None,
                    partial(
                        run_agenda,
                        executable=args.lark_cli,
                        timeout=args.calendar_timeout,
                        metadata=agenda_metadata,
                    ),
                ),
                loop.run_in_executor(
                    None,
                    partial(
                        run_profile,
                        executable=args.lark_cli,
                        timeout=args.calendar_timeout,
                        avatar_timeout=args.calendar_timeout,
                    ),
                ),
                loop.run_in_executor(
                    None,
                    partial(
                        run_recent_tasks,
                        query_time,
                        executable=args.lark_cli,
                        timeout=args.calendar_timeout,
                    ),
                ),
                loop.run_in_executor(
                    None,
                    partial(
                        run_unread_count,
                        timeout=args.calendar_timeout,
                    ),
                ),
            )
        )
        emit_progress(
            args,
            "collecting",
            "calendar_ready",
            owner_calendar_count=agenda_metadata.get(
                "owner_calendar_count", 0
            ),
            fetched_event_count=agenda_metadata.get(
                "event_count", len(events)
            ),
        )
        emit_progress(
            args,
            "collecting",
            "profile_ready",
            avatar_present=bool(profile.avatar_rgb565),
            avatar_bytes=(
                AVATAR_RGB565_BYTES if profile.avatar_rgb565 else 0
            ),
        )
        emit_progress(
            args,
            "collecting",
            "tasks_ready",
            overdue_task_count=len(tasks),
        )
        emit_progress(
            args,
            "collecting",
            "unread_ready",
            unread_message_count=unread_message_count,
        )
        summary = build_summary(
            events,
            args.user_name or profile.name,
            datetime.now().astimezone(),
            tasks=tasks,
            avatar_rgb565=profile.avatar_rgb565,
            overdue_task_count=len(tasks),
            unread_message_count=unread_message_count,
        )
        emit_progress(
            args,
            "summarizing",
            "summary_ready",
            fetched_event_count=len(events),
            selected_event_count=len(summary["events"]),
            selected_task_count=len(summary["tasks"]),
            completed_event_count=sum(
                bool(event["completed"]) for event in summary["events"]
            ),
            all_day_event_count=sum(
                bool(event["all_day"]) for event in summary["events"]
            ),
        )
        payload = encode_summary(summary)
        transfer = build_transfer(payload, args.chunk_size)
        emit_progress(
            args,
            "summarizing",
            "payload_ready",
            protocol_version=PROTOCOL_VERSION,
            payload_bytes=len(payload),
            payload_limit_bytes=MAX_PAYLOAD_BYTES,
            chunk_bytes=args.chunk_size,
            data_frame_count=transfer.data_frame_count,
            total_frame_count=len(transfer.frames),
            checksum_hex=f"{transfer.checksum:08x}",
        )
        transport = PassportBleTransport(
            device_name=args.device_name,
            device_identifier=args.device,
            service_uuid=args.service_uuid,
            write_uuid=args.write_uuid,
            status_uuid=args.status_uuid,
            scan_timeout=args.scan_timeout,
            connect_timeout=args.connect_timeout,
            acknowledge_timeout=args.ack_timeout,
        )
        await transport.send(
            transfer,
            progress=lambda phase, **details: emit_progress(
                args,
                phase,
                details.pop("event", None),
                **details,
            ),
        )
    except (
        CalendarSourceError,
        ProfileSourceError,
        TaskSourceError,
        UnreadSourceError,
        SummaryError,
        ProtocolError,
        BleTransportError,
    ) as exc:
        raise BridgeError(str(exc)) from exc

    return summary, len(payload)


def emit_progress(
    args: argparse.Namespace,
    phase: str,
    event: Optional[str] = None,
    **details: Any,
) -> None:
    if args.progress_json:
        document: Dict[str, Any] = {
            "type": "progress",
            "phase": phase,
        }
        if event:
            document["event"] = event
        if details:
            document["details"] = details
        print(
            json.dumps(
                document,
                ensure_ascii=False,
                separators=(",", ":"),
            ),
            flush=True,
        )


def print_result(
    summary: Dict[str, Any], payload_bytes: int, json_output: bool
) -> None:
    if json_output:
        print(
            json.dumps(
                {
                    "ok": True,
                    "summary": summary,
                    "payload_bytes": payload_bytes,
                },
                ensure_ascii=False,
                separators=(",", ":"),
                sort_keys=True,
            )
        )
        return
    print(
        f"Sync complete: {len(summary['events'])} event(s), "
        f"{len(summary['tasks'])} recent task(s), "
        f"{summary['unread_message_count']} unread, "
        f"{payload_bytes} payload bytes."
    )


async def run(args: argparse.Namespace) -> int:
    if args.interval is None:
        try:
            summary, payload_bytes = await synchronize_once(args)
        except BridgeError as exc:
            if args.json_output:
                print(json.dumps({"ok": False, "error": str(exc)}))
            else:
                print(f"Sync failed: {exc}", file=sys.stderr)
            return 1
        print_result(summary, payload_bytes, args.json_output)
        return 0

    while True:
        try:
            summary, payload_bytes = await synchronize_once(args)
            print_result(summary, payload_bytes, args.json_output)
        except BridgeError as exc:
            if args.json_output:
                print(json.dumps({"ok": False, "error": str(exc)}))
            else:
                print(f"Sync failed: {exc}", file=sys.stderr)
        await asyncio.sleep(args.interval)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return asyncio.run(run(args))
    except KeyboardInterrupt:
        return 130
