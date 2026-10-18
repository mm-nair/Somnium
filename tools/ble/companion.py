#!/usr/bin/env python3
"""Somnium phone-link CLI. Talks to the camera GATT service.

Requires bleak (not part of the Nix shell):

    pip install bleak
    python tools/ble/companion.py status
    python tools/ble/companion.py modes
    python tools/ble/companion.py mode anime
    python tools/ble/companion.py mode 2
    python tools/ble/companion.py shutter
    python tools/ble/companion.py watch --seconds 90
"""

from __future__ import annotations

import argparse
import asyncio
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

from framing import (  # noqa: E402
    DREAM_UUID,
    MODE_SELECT_UUID,
    MODES_UUID,
    PREVIEW_HEIGHT,
    PREVIEW_UUID,
    PREVIEW_WIDTH,
    SERVICE_UUID,
    SHUTTER_UUID,
    STATUS_UUID,
    PreviewAssembler,
    rgb565_ppm,
    unpack_dream,
    unpack_modes,
    unpack_status,
)


def _bleak():
    try:
        import bleak  # noqa: F401
    except ImportError as error:
        raise SystemExit(
            "bleak is not installed. pip install bleak, then retry."
        ) from error
    from bleak import BleakClient, BleakScanner

    return BleakClient, BleakScanner


async def find_device(timeout: float):
    _, BleakScanner = _bleak()
    print("scanning for Somnium…", flush=True)
    discovered = await BleakScanner.discover(timeout=timeout, return_adv=True)
    if isinstance(discovered, dict):
        pairs = list(discovered.items())
    else:
        pairs = [(found, None) for found in discovered]
    for found, adv in pairs:
        uuids = []
        if adv is not None:
            uuids = [str(u).lower() for u in (getattr(adv, "service_uuids", None) or [])]
        if (found.name or "") == "Somnium" or SERVICE_UUID in uuids:
            return found
    return None


async def connect(timeout: float):
    BleakClient, _ = _bleak()
    device = await find_device(timeout)
    if device is None:
        raise SystemExit("no Somnium advertisement. Is the camera on, with SOMNIUM_BLE=1?")
    print(f"connecting to {device.name or 'Somnium'} {device.address}", flush=True)
    client = BleakClient(device)
    await client.connect()
    return client


def _print_status(data: bytes, modes: dict | None) -> None:
    status = unpack_status(data)
    label = ""
    style_id = ""
    if modes and modes.get("modes"):
        for mode in modes["modes"]:
            if mode["index"] == status["mode_index"]:
                label = mode["label"]
                style_id = mode["id"]
                break
    battery = (
        "no gauge"
        if status["battery_pct"] is None
        else f"{status['battery_pct']}%"
    )
    print(
        f"screen={status['screen_name']} mode={status['mode_index']}"
        f"{(' ' + style_id) if style_id else ''}"
        f"{(' ' + label) if label else ''} "
        f"busy={int(status['busy'])} failed={int(status['failed'])} "
        f"camera={int(status['camera_ok'])} sd={int(status['sd_ready'])} "
        f"sd_photos={status['sd_count']} ram_photos={status['ram_count']} "
        f"dream={int(status['dream_ready'])} battery={battery}",
        flush=True,
    )


def _print_modes(data: bytes) -> dict:
    modes = unpack_modes(data)
    if modes["updated"]:
        print("modes updated (read again)", flush=True)
        return modes
    print(f"selected={modes['selected']} count={len(modes['modes'])}", flush=True)
    for mode in modes["modes"]:
        mark = "*" if mode["index"] == modes["selected"] else " "
        print(f" {mark} {mode['index']} {mode['id']}  {mode['label']}", flush=True)
    return modes


async def cmd_status(args: argparse.Namespace) -> None:
    client = await connect(args.timeout)
    try:
        modes = _print_modes(bytes(await client.read_gatt_char(MODES_UUID)))
        _print_status(bytes(await client.read_gatt_char(STATUS_UUID)), modes)
        dream = unpack_dream(bytes(await client.read_gatt_char(DREAM_UUID)))
        print(
            f"dream={dream['state_name']} generation={dream['generation']} "
            f"jpeg={dream['dream_bytes']} thumb={dream['thumb_width']}x{dream['thumb_height']}",
            flush=True,
        )
    finally:
        await client.disconnect()


async def cmd_modes(args: argparse.Namespace) -> None:
    client = await connect(args.timeout)
    try:
        _print_modes(bytes(await client.read_gatt_char(MODES_UUID)))
    finally:
        await client.disconnect()


async def cmd_mode(args: argparse.Namespace) -> None:
    text = args.style.strip()
    if text.isdigit():
        payload = bytes([int(text) & 0xFF])
    else:
        payload = text.encode("ascii")
    client = await connect(args.timeout)
    try:
        await client.write_gatt_char(MODE_SELECT_UUID, payload, response=True)
        await asyncio.sleep(0.3)
        _print_modes(bytes(await client.read_gatt_char(MODES_UUID)))
    finally:
        await client.disconnect()


async def cmd_shutter(args: argparse.Namespace) -> None:
    client = await connect(args.timeout)
    try:
        await client.write_gatt_char(SHUTTER_UUID, bytes([1]), response=True)
        print("shutter", flush=True)
    finally:
        await client.disconnect()


async def cmd_watch(args: argparse.Namespace) -> None:
    client = await connect(args.timeout)
    assembler = PreviewAssembler()
    modes: dict = {"modes": []}
    out = Path(args.out)

    def on_status(_char, data: bytearray) -> None:
        try:
            _print_status(bytes(data), modes)
        except ValueError as error:
            print(f"status? {error}", flush=True)

    async def refresh_modes() -> None:
        nonlocal modes
        modes = _print_modes(bytes(await client.read_gatt_char(MODES_UUID)))

    def on_modes(_char, data: bytearray) -> None:
        nonlocal modes
        try:
            parsed = unpack_modes(bytes(data))
        except ValueError as error:
            print(f"modes? {error}", flush=True)
            return
        if parsed["updated"]:
            asyncio.create_task(refresh_modes())
            return
        modes = parsed
        _print_modes(bytes(data))

    def on_dream(_char, data: bytearray) -> None:
        try:
            dream = unpack_dream(bytes(data))
        except ValueError as error:
            print(f"dream? {error}", flush=True)
            return
        print(
            f"dream {dream['state_name']} gen={dream['generation']} "
            f"jpeg={dream['dream_bytes']} "
            f"thumb={dream['thumb_width']}x{dream['thumb_height']}",
            flush=True,
        )
        assembler.reset()

    def on_preview(_char, data: bytearray) -> None:
        try:
            image = assembler.push(bytes(data))
        except ValueError as error:
            print(f"preview? {error}", flush=True)
            return
        if image is None:
            return
        width = PREVIEW_WIDTH
        height = PREVIEW_HEIGHT
        out.write_bytes(rgb565_ppm(image, width, height))
        print(f"preview {width}x{height} -> {out}", flush=True)

    try:
        modes = _print_modes(bytes(await client.read_gatt_char(MODES_UUID)))
        _print_status(bytes(await client.read_gatt_char(STATUS_UUID)), modes)
        await client.start_notify(STATUS_UUID, on_status)
        await client.start_notify(MODES_UUID, on_modes)
        await client.start_notify(DREAM_UUID, on_dream)
        await client.start_notify(PREVIEW_UUID, on_preview)
        print(f"watching {args.seconds:.0f}s. Ctrl-C stops.", flush=True)
        await asyncio.sleep(args.seconds)
    finally:
        await client.disconnect()


def main() -> int:
    parser = argparse.ArgumentParser(description="Somnium BLE companion")
    parser.add_argument("--timeout", type=float, default=8.0, help="scan seconds")
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status").set_defaults(func=cmd_status)
    sub.add_parser("modes").set_defaults(func=cmd_modes)
    mode = sub.add_parser("mode")
    mode.add_argument("style", help="style id (anime) or index")
    mode.set_defaults(func=cmd_mode)
    sub.add_parser("shutter").set_defaults(func=cmd_shutter)
    watch = sub.add_parser("watch")
    watch.add_argument("--seconds", type=float, default=60.0)
    watch.add_argument("--out", default="somnium-preview.ppm")
    watch.set_defaults(func=cmd_watch)
    args = parser.parse_args()
    try:
        asyncio.run(args.func(args))
    except KeyboardInterrupt:
        return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
