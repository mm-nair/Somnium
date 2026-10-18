# Somnium BLE

The camera advertises as **Somnium** and exposes one custom GATT service. A phone can list the style presets, switch the selected one, press the shutter, subscribe to status, and receive a notice plus a small preview when a dream is ready.

The cook itself does not move to the phone. The shutter still runs the existing path: Process, USB `COOK` to the laptop, `DREAM` back, Review. BLE only posts input events and reads state. It never writes the USB serial port.

`SOMNIUM_BLE` defaults to `1` in `platformio.ini`. Set it to `0` and rebuild for a USB-only image (`ble off` on the serial log). NimBLE-Arduino is not linked in that build.

## Why NimBLE

espressif32 7.1.3 is Arduino-ESP32 2.0.17, so this is NimBLE-Arduino **1.4.3**. NimBLE 2.x needs Arduino core 3 and does not build on this platform.

The stock ESP32 BLE library is Bluedroid. NimBLE is the lighter host: about half the flash of Bluedroid and on the order of 100 KB less RAM for the same peripheral role, which is the figure the library documents. This board already spends internal RAM on the camera driver and a 64 KB USB receive buffer. Dream and capture JPEGs are allocated in PSRAM. The NimBLE flags in `platformio.ini` keep the host on that side of the split:

- one connection, one bond, six CCCDs
- peripheral and broadcaster only (no central, no scanner)
- preferred ATT MTU 185
- host allocations in external PSRAM (`CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL`)
- host logs compiled out

The controller still uses internal RAM. That is a chip requirement, not something the host library can move.

Same tree, `pio run`, espressif32 7.1.3:

| Image | Static RAM | Flash |
| --- | --- | --- |
| `SOMNIUM_BLE=0` | 41.4 KB (12.6% of 320 KB) | 697 KB |
| `SOMNIUM_BLE=1` | 51.8 KB (15.8%) | 915 KB |

The phone link adds about 10 KB of static RAM and 213 KB of flash. Runtime host pools are not in that static figure; they come from PSRAM. The BLE image's symbol table contains NimBLE and no Bluedroid symbols.

## How commands enter the firmware

Writes are handled on the NimBLE host task. They do not touch the UI, the camera, or `Serial`. A shutter write posts `InputEvent::ShutterReleased`. A style write posts `InputEvent::ModeRequested`. `App` drains both through `Inputs::pollEvent`, the same loop that drains the button and the dial.

- Shutter from Mode, Storage, or Review starts a shot. On Process it is ignored, including the error hold, matching the physical button (`requestCapture` returns while that screen is up).
- A style change on the mode screen animates with `ModePage::nudge`. On Review, Storage, or Boot it snaps with `ModePage::selectIndex`, so a hidden carousel does not depend on `update()` to finish the move.
- Style writes during an in-flight cook are dropped. The cook already captured `gMode.selected().id` when it called `Host::cook`.

## GATT

UUIDs are 128-bit. The bytes `534f4d4e` / `534f4d4e494d` are the ASCII fragments `SOMN` and `SOMNIM`.

| Characteristic | UUID | Properties |
| --- | --- | --- |
| Service | `534f4d4e-1000-4000-8000-534f4d4e494d` | |
| Modes | `534f4d4e-1001-4000-8000-534f4d4e494d` | Read, Notify |
| Mode select | `534f4d4e-1002-4000-8000-534f4d4e494d` | Write, Write without response |
| Shutter | `534f4d4e-1003-4000-8000-534f4d4e494d` | Write, Write without response |
| Status | `534f4d4e-1004-4000-8000-534f4d4e494d` | Read, Notify |
| Dream | `534f4d4e-1005-4000-8000-534f4d4e494d` | Read, Notify |
| Preview | `534f4d4e-1006-4000-8000-534f4d4e494d` | Notify |

No pairing. One connection. On disconnect the camera advertises again. The device name in the advertising / scan response is `Somnium`.

All multi-byte integers are little-endian. Strings are ASCII.

### Modes

```
u8 selected index
u8 count
repeated count times:
  u8 index
  u8 id length
  id bytes          (style id, e.g. anime)
  u8 label length
  label bytes       (on-screen name, e.g. ANIME)
```

Current firmware order is the `kModes` table: `popcomic` / COMIC, `watercolor` / WATERCOLOR, `anime` / ANIME, `surprise` / SURPRISE, `pencil` / PENCIL SKETCH.

Read always returns that blob (Read Blob covers a long value at MTU 23). A notification is the same blob when it fits in `ATT_MTU - 3`. When it does not, the notification is the single byte `0x01` and the client should Read.

### Mode select

- 1 byte: mode index. Out of range is ignored.
- 2 or more bytes: style id, exact ASCII match (`anime`, not `ANIME`). Unknown ids are ignored.

Ignored while a cook is in progress (Process, and not the error hold).

### Shutter

One or more bytes. A non-zero first byte is a shutter release. `0x00` is ignored. Ignored while the Process screen is up.

### Status

10 bytes, so a notification fits at the default MTU.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | version, `1` |
| 1 | 1 | screen: 0 boot, 1 mode, 2 process, 3 review, 4 storage |
| 2 | 1 | selected mode index |
| 3 | 1 | flags |
| 4 | 2 | SD photo count |
| 6 | 2 | RAM gallery count (pairs that failed to hit the card) |
| 8 | 1 | battery percent, or `0xFF` if there is no gauge |
| 9 | 1 | reserved, `0` |

Flags:

| Bit | Meaning |
| --- | --- |
| 0 | busy (Process, cook in progress) |
| 1 | failed (Process error hold) |
| 2 | SD mounted |
| 3 | camera initialized |
| 4 | Review is showing a dream |
| 5 | battery gauge present |

This board has no battery divider. Bit 5 stays clear and byte 8 stays `0xFF`.

The on-device browser shows SD photos first, then RAM fallbacks. `sd_count + ram_count` is that list.

### Dream

20 bytes. Notified when a dream is shown and when the cook fails. Also readable.

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | state: 0 idle, 1 ready, 2 failed |
| 1 | 1 | mode index used for the shot |
| 2 | 4 | full dream JPEG size on the camera, not the preview |
| 6 | 2 | thumbnail width |
| 8 | 2 | thumbnail height |
| 10 | 1 | pixel format, `1` = RGB565 little-endian |
| 11 | 1 | reserved, `0` |
| 12 | 4 | thumbnail byte count, `width * height * 2`, or 0 |
| 16 | 2 | generation, increments on ready and on failure |
| 18 | 2 | reserved, `0` |

A ready notice is sent after the USB transfer has been ACKed. The full JPEG stays on the USB path and on the SD card.

### Preview

Notifications only. RGB565 bytes for a 40×30 thumbnail (the dream is 320×240, so this is an exact 1/8 scale). The firmware decodes with the same `Image::decodeRgb565` path as the panel, which stays on TJpg_Decoder.

Each notification:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | flags |
| 1 | 2 | sequence, starting at 0 for each image |
| 3 | 2 | payload length |
| 5 | payload length | payload |

Flags: bit 0 start, bit 1 end, bit 7 abort. Start and end may both be set when the image fits in one notification.

Start payload begins with 9 meta bytes, then image bytes:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | total image bytes |
| 4 | 2 | width |
| 6 | 2 | height |
| 8 | 1 | format (`1` = RGB565) |

Later payloads are raw image bytes. The chunk size is limited to `min(ATT_MTU - 3, 244)`. At MTU 23 the first chunk carries 6 image bytes; at MTU 185 it carries 168. Chunks are paced at about one per 40 ms so the host task is not flooded. Sequence numbers are contiguous. An abort frame has payload length 0 and means the partial image should be dropped. A new dream also begins with a start frame.

Reassembly: reset on abort or on a start frame. Append image bytes. Accept the buffer when the end flag is set and the length matches `total`.

## Clients

`tools/ble/companion.html` is a single Web Bluetooth page: mode buttons, shutter, live status, and the preview scaled up on a canvas. Open it in Chrome on a computer or Android (not Safari or Firefox). The page has to be served or opened as a file from a click, because Web Bluetooth requires a user gesture.

```sh
python3 -m http.server -d tools/ble 8000
# Chrome: http://localhost:8000/companion.html
```

`tools/ble/companion.py` is a bleak CLI. Bleak is not in the Nix shell.

```sh
pip install bleak
python3 tools/ble/companion.py status
python3 tools/ble/companion.py mode anime
python3 tools/ble/companion.py shutter
python3 tools/ble/companion.py watch --seconds 90
```

`watch` writes a finished preview to `somnium-preview.ppm`.

## Build

```sh
nix develop -c pio run
nix develop -c python -m unittest discover -s tests -p 'test_*.py' -v
```

Flash with `./scripts/flash.sh` while the USB bridge is stopped. The serial log prints `ble ok` before `READY usb-cook`. Shoot from the phone only while `./scripts/usb_bridge.sh` and the server are running; otherwise Process ends on the usual bridge error and the dream characteristic reports failure.

Host tests compile `BleProto.cpp` with g++ and check the Python codec against that dump. They do not need a board or a radio.

## Not in this pass

- The full dream JPEG over BLE. The preview is 40×30 RGB565.
- Encryption, pairing, or more than one phone.
- A battery reading. Nothing on this PCB measures one.
- Wi-Fi, the offline queue, power management, and the server-side style engine. Those are separate.
