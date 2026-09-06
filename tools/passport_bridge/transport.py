"""Bleak transport for the passport summary protocol."""

from __future__ import annotations

import asyncio
from typing import Any, Optional, Tuple

from .protocol import (
    DATA_HEADER_STRUCT,
    DEVICE_NAME,
    SERVICE_UUID,
    STATUS_CHARACTERISTIC_UUID,
    COMPLETIONS_CHARACTERISTIC_UUID,
    COMPLETIONS_ACK_CHARACTERISTIC_UUID,
    PROTOCOL_CHARACTERISTIC_UUID,
    PROTOCOL_VERSION,
    WRITE_CHARACTERISTIC_UUID,
    FrameResult,
    ProtocolError,
    Transfer,
    cancel_frame,
    parse_status,
    verify_commit,
)


class BleTransportError(RuntimeError):
    """A sanitized BLE discovery, connection, or transfer failure."""


def _is_missing_characteristic(error: Exception, characteristic_uuid: str) -> bool:
    return (
        type(error).__name__ == "BleakCharacteristicNotFoundError"
        and characteristic_uuid.lower() in str(error).lower()
    )


def _load_bleak() -> Tuple[Any, Any]:
    try:
        from bleak import BleakClient, BleakScanner  # type: ignore[import-not-found]
    except ImportError as exc:
        raise BleTransportError(
            "bleak is not installed; install tools/passport_bridge/requirements.txt"
        ) from exc
    return BleakClient, BleakScanner


class PassportBleTransport:
    def __init__(
        self,
        device_name: str = DEVICE_NAME,
        device_identifier: Optional[str] = None,
        service_uuid: str = SERVICE_UUID,
        write_uuid: str = WRITE_CHARACTERISTIC_UUID,
        status_uuid: str = STATUS_CHARACTERISTIC_UUID,
        completions_uuid: str = COMPLETIONS_CHARACTERISTIC_UUID,
        completions_ack_uuid: str = COMPLETIONS_ACK_CHARACTERISTIC_UUID,
        protocol_uuid: str = PROTOCOL_CHARACTERISTIC_UUID,
        scan_timeout: float = 10.0,
        connect_timeout: float = 15.0,
        acknowledge_timeout: float = 8.0,
    ) -> None:
        self.device_name = device_name
        self.device_identifier = device_identifier
        self.service_uuid = service_uuid.lower()
        self.write_uuid = write_uuid
        self.status_uuid = status_uuid
        self.completions_uuid = completions_uuid
        self.completions_ack_uuid = completions_ack_uuid
        self.protocol_uuid = protocol_uuid
        self.scan_timeout = scan_timeout
        self.connect_timeout = connect_timeout
        self.acknowledge_timeout = acknowledge_timeout

    async def _find_device(self, scanner: Any) -> Any:
        if self.device_identifier:
            return await scanner.find_device_by_address(
                self.device_identifier, timeout=self.scan_timeout
            )

        def matches(device: Any, advertisement: Any) -> bool:
            name = advertisement.local_name or getattr(device, "name", None)
            service_uuids = {
                value.lower() for value in (advertisement.service_uuids or [])
            }
            return name == self.device_name and self.service_uuid in service_uuids

        return await scanner.find_device_by_filter(
            matches, timeout=self.scan_timeout
        )

    async def _wait_for_commit(
        self, client: Any, notifications: asyncio.Queue
    ) -> None:
        loop = asyncio.get_running_loop()
        deadline = loop.time() + self.acknowledge_timeout
        while True:
            remaining = deadline - loop.time()
            if remaining <= 0:
                break
            try:
                raw_status = await asyncio.wait_for(
                    notifications.get(), timeout=remaining
                )
            except asyncio.TimeoutError:
                break
            status = parse_status(raw_status)
            if status.result is not FrameResult.ACCEPTED:
                verify_commit(status)
                return

        try:
            status = parse_status(bytes(await client.read_gatt_char(self.status_uuid)))
            verify_commit(status)
        except ProtocolError as exc:
            raise BleTransportError(str(exc)) from exc
        except Exception as exc:
            raise BleTransportError(
                "device did not acknowledge the committed transfer "
                f"({type(exc).__name__}: {exc})"
            ) from exc

    async def send(self, transfer: Transfer, progress=None, completion_handler=None) -> None:
        """Discover, connect, send all frames, and verify the commit status."""
        client_class, scanner_class = _load_bleak()
        if progress:
            progress(
                "connecting",
                event="scan_started",
                device_name=self.device_name,
                discovery_mode=(
                    "identifier"
                    if self.device_identifier
                    else "advertisement"
                ),
                scan_timeout_seconds=self.scan_timeout,
            )
        try:
            device = await self._find_device(scanner_class)
        except Exception as exc:
            raise BleTransportError("BLE scan failed") from exc
        if device is None:
            raise BleTransportError("passport BLE service was not found")
        if progress:
            progress(
                "connecting",
                event="device_found",
                device_name=self.device_name,
            )

        notifications: asyncio.Queue = asyncio.Queue()
        loop = asyncio.get_running_loop()

        def on_status(sender: Any, data: bytearray) -> None:
            del sender
            raw_status = bytes(data)
            loop.call_soon_threadsafe(notifications.put_nowait, raw_status)

        began = False
        notifications_started = False
        client = None
        try:
            async with client_class(
                device, timeout=self.connect_timeout
            ) as client:
                if progress:
                    progress(
                        "connecting",
                        event="connected",
                        connect_timeout_seconds=self.connect_timeout,
                    )
                # CoreBluetooth initiates pairing when an authenticated
                # characteristic is read. Do this before any write attempt.
                await client.read_gatt_char(self.status_uuid)
                try:
                    protocol = bytes(
                        await client.read_gatt_char(self.protocol_uuid)
                    )
                except Exception as exc:
                    raise BleTransportError(
                        "device does not expose a BLE protocol version; "
                        "flash firmware with BLE protocol v"
                        f"{PROTOCOL_VERSION}"
                    ) from exc
                if len(protocol) != 1 or protocol[0] != PROTOCOL_VERSION:
                    device_version = protocol[0] if len(protocol) == 1 else "invalid"
                    raise BleTransportError(
                        "BLE protocol version mismatch: "
                        f"host v{PROTOCOL_VERSION}, device v{device_version}"
                    )
                if progress:
                    progress(
                        "connecting",
                        event="protocol_version_checked",
                        host_protocol_version=PROTOCOL_VERSION,
                        device_protocol_version=protocol[0],
                    )
                if completion_handler:
                    try:
                        raw = bytes(
                            await client.read_gatt_char(self.completions_uuid)
                        )
                    except Exception as exc:
                        if not _is_missing_characteristic(exc, self.completions_uuid):
                            raise
                        if progress:
                            progress(
                                "connecting",
                                event="completion_queue_unavailable",
                            )
                    else:
                        guids = [
                            item
                            for item in raw.decode("ascii", "ignore").splitlines()
                            if item and len(item) < 64
                        ]
                        for guid in dict.fromkeys(guids):
                            try:
                                completed = await completion_handler(guid)
                            except Exception as exc:
                                if progress:
                                    progress(
                                        "connecting",
                                        event="completion_failed",
                                        task_guid=guid,
                                        error_type=type(exc).__name__,
                                    )
                                continue
                            if completed:
                                try:
                                    await client.write_gatt_char(
                                        self.completions_ack_uuid,
                                        guid.encode("ascii"),
                                        response=True,
                                    )
                                except Exception as exc:
                                    if progress:
                                        progress(
                                            "connecting",
                                            event="completion_ack_failed",
                                            task_guid=guid,
                                            error_type=type(exc).__name__,
                                        )
                await client.start_notify(self.status_uuid, on_status)
                notifications_started = True
                if progress:
                    progress(
                        "connecting",
                        event="notifications_ready",
                        ack_timeout_seconds=self.acknowledge_timeout,
                    )
                    progress(
                        "transferring",
                        event="transfer_started",
                        payload_bytes=len(transfer.payload),
                        data_frame_count=transfer.data_frame_count,
                        total_frame_count=len(transfer.frames),
                    )
                for index, frame in enumerate(transfer.frames):
                    await client.write_gatt_char(
                        self.write_uuid, frame, response=True
                    )
                    if index == 0:
                        began = True
                        if progress:
                            progress(
                                "transferring",
                                event="begin_sent",
                                payload_bytes=len(transfer.payload),
                                checksum_hex=f"{transfer.checksum:08x}",
                            )
                    elif index <= transfer.data_frame_count:
                        if progress:
                            progress(
                                "transferring",
                                event="chunk_sent",
                                frame_index=index,
                                frame_total=transfer.data_frame_count,
                                data_bytes=(
                                    len(frame) - DATA_HEADER_STRUCT.size
                                ),
                            )
                    elif progress:
                        progress(
                            "transferring",
                            event="commit_sent",
                        )
                await self._wait_for_commit(client, notifications)
                if progress:
                    progress(
                        "complete",
                        event="ack_received",
                        result="COMPLETE",
                    )
                try:
                    await client.stop_notify(self.status_uuid)
                except Exception:
                    pass
                notifications_started = False
        except Exception as exc:
            if began and client is not None:
                try:
                    await client.write_gatt_char(
                        self.write_uuid, cancel_frame(), response=True
                    )
                except Exception:
                    pass
            if notifications_started and client is not None:
                try:
                    await client.stop_notify(self.status_uuid)
                except Exception:
                    pass
            if isinstance(exc, BleTransportError):
                raise
            if isinstance(exc, ProtocolError):
                raise BleTransportError(str(exc)) from exc
            raise BleTransportError(
                f"BLE transfer failed ({type(exc).__name__}: {exc})"
            ) from exc
