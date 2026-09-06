"""Read the current Feishu profile and convert its avatar for the passport."""

from __future__ import annotations

import base64
import io
import json
import subprocess
import urllib.request
from dataclasses import dataclass
from typing import Any, Callable, List


AVATAR_WIDTH = 32
AVATAR_HEIGHT = 32
AVATAR_RGB565_BYTES = AVATAR_WIDTH * AVATAR_HEIGHT * 2
MAX_AVATAR_DOWNLOAD_BYTES = 5 * 1024 * 1024


class ProfileSourceError(RuntimeError):
    """A sanitized profile-source failure safe to show in bridge logs."""


@dataclass(frozen=True)
class Profile:
    name: str
    avatar_rgb565: str


def profile_command(executable: str = "lark-cli") -> List[str]:
    """Return the fixed, credential-free profile command."""
    return [
        executable,
        "contact",
        "+get-user",
        "--as",
        "user",
        "--json",
    ]


def _failure(status: str) -> ProfileSourceError:
    return ProfileSourceError(f"lark-cli profile failed (status: {status})")


def parse_profile_output(raw_output: str) -> tuple[str, str]:
    """Extract only the display name and thumbnail URL from structured output."""
    try:
        document = json.loads(raw_output)
    except (TypeError, json.JSONDecodeError) as exc:
        raise _failure("invalid-json") from exc

    data = document.get("data") if isinstance(document, dict) else None
    if (
        not isinstance(document, dict)
        or document.get("ok") is not True
        or not isinstance(data, dict)
    ):
        raise _failure("invalid-response")
    profile = data.get("user", data)
    if not isinstance(profile, dict):
        raise _failure("invalid-response")

    name = profile.get("name")
    avatar_thumb = profile.get("avatar_thumb", "")
    if not isinstance(name, str) or not name.strip():
        raise _failure("missing-name")
    if avatar_thumb is None:
        avatar_thumb = ""
    if not isinstance(avatar_thumb, str):
        raise _failure("invalid-avatar")
    return name.strip(), avatar_thumb.strip()


def avatar_bytes_to_rgb565(raw_image: bytes) -> str:
    """Center-crop an image to 32x32 and return base64 little-endian RGB565."""
    try:
        from PIL import Image, ImageOps
    except ImportError as exc:
        raise _failure("pillow-not-installed") from exc

    try:
        with Image.open(io.BytesIO(raw_image)) as source:
            source.load()
            image = ImageOps.fit(
                source.convert("RGB"),
                (AVATAR_WIDTH, AVATAR_HEIGHT),
                method=Image.Resampling.LANCZOS,
                centering=(0.5, 0.5),
            )
            pixels = bytearray()
            rgb_bytes = image.tobytes()
            for offset in range(0, len(rgb_bytes), 3):
                red, green, blue = rgb_bytes[offset : offset + 3]
                rgb565 = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)
                pixels.extend((rgb565 & 0xFF, rgb565 >> 8))
    except Exception as exc:
        raise _failure("invalid-avatar-image") from exc

    if len(pixels) != AVATAR_RGB565_BYTES:
        raise _failure("invalid-avatar-size")
    return base64.b64encode(pixels).decode("ascii")


def fetch_avatar_rgb565(
    avatar_thumb: str,
    timeout: float = 15.0,
    opener: Callable[..., Any] = urllib.request.urlopen,
) -> str:
    """Download and convert an avatar, returning the empty fallback on failure."""
    if not avatar_thumb:
        return ""
    try:
        request = urllib.request.Request(
            avatar_thumb,
            headers={"User-Agent": "FoloPassportBridge/2"},
        )
        with opener(request, timeout=timeout) as response:
            raw_image = response.read(MAX_AVATAR_DOWNLOAD_BYTES + 1)
        if len(raw_image) > MAX_AVATAR_DOWNLOAD_BYTES:
            return ""
        return avatar_bytes_to_rgb565(raw_image)
    except Exception:
        return ""


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


def run_profile(
    executable: str = "lark-cli",
    timeout: float = 30.0,
    avatar_timeout: float = 15.0,
    runner: Callable[..., subprocess.CompletedProcess] = subprocess.run,
    opener: Callable[..., Any] = urllib.request.urlopen,
) -> Profile:
    """Return the current name and converted avatar without retaining its URL."""
    name, avatar_thumb = parse_profile_output(
        _run(profile_command(executable), timeout, runner)
    )
    return Profile(
        name=name,
        avatar_rgb565=fetch_avatar_rgb565(
            avatar_thumb,
            timeout=avatar_timeout,
            opener=opener,
        ),
    )
