"""Send a caller-provided, protocol-v4 work summary over the existing BLE link."""

from __future__ import annotations

import argparse
import asyncio
import base64
import binascii
import json
from typing import Any, Dict, Optional, Sequence, Tuple

from .cli import BridgeError, emit_progress, print_result
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
from .transport import BleTransportError, PassportBleTransport


def _chunk_size(value: str) -> int:
    parsed = int(value)
    if not 1 <= parsed <= 509:
        raise argparse.ArgumentTypeError("chunk size must be between 1 and 509")
    return parsed


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="python3 -m tools.passport_bridge.demo",
        description="Send a locally authored AI Passport work summary over BLE.",
    )
    parser.add_argument("--summary-base64", required=True)
    parser.add_argument("--device")
    parser.add_argument("--json-output", action="store_true")
    parser.add_argument("--progress-json", action="store_true")
    parser.add_argument("--device-name", default=DEVICE_NAME)
    parser.add_argument("--service-uuid", default=SERVICE_UUID)
    parser.add_argument("--write-uuid", default=WRITE_CHARACTERISTIC_UUID)
    parser.add_argument("--status-uuid", default=STATUS_CHARACTERISTIC_UUID)
    parser.add_argument("--chunk-size", type=_chunk_size, default=DEFAULT_CHUNK_BYTES)
    return parser


def _decode_summary(value: str) -> Dict[str, Any]:
    try:
        raw = base64.b64decode(value, validate=True)
        summary = json.loads(raw.decode("utf-8"))
    except (ValueError, UnicodeDecodeError, binascii.Error, json.JSONDecodeError) as exc:
        raise BridgeError("演示数据格式无效。") from exc
    if not isinstance(summary, dict):
        raise BridgeError("演示数据必须是一个摘要对象。")
    return summary


async def synchronize_once(args: argparse.Namespace) -> Tuple[Dict[str, Any], int]:
    summary = _decode_summary(args.summary_base64)
    try:
        emit_progress(args, "collecting", "demo_ready", source_count=1, date=summary.get("date", ""))
        payload = encode_summary(summary)
        transfer = build_transfer(payload, args.chunk_size)
        emit_progress(
            args,
            "summarizing",
            "summary_ready",
            fetched_event_count=len(summary["events"]),
            selected_event_count=len(summary["events"]),
            selected_task_count=len(summary["tasks"]),
            completed_event_count=sum(bool(event["completed"]) for event in summary["events"]),
            all_day_event_count=sum(bool(event["all_day"]) for event in summary["events"]),
        )
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
        )
        await transport.send(
            transfer,
            progress=lambda phase, **details: emit_progress(
                args, phase, details.pop("event", None), **details
            ),
        )
    except (KeyError, TypeError, ProtocolError, BleTransportError) as exc:
        raise BridgeError(str(exc)) from exc
    return summary, len(payload)


async def run(args: argparse.Namespace) -> int:
    try:
        summary, payload_bytes = await synchronize_once(args)
    except BridgeError as exc:
        if args.json_output:
            print(json.dumps({"ok": False, "error": str(exc)}, ensure_ascii=False))
        return 1
    print_result(summary, payload_bytes, args.json_output)
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return asyncio.run(run(args))
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
