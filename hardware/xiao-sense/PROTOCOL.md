# Somnium phone protocol (XIAO ESP32S3 Sense)

The camera does not join the home network and does not call the image API.
The phone does. This document is the contract between the firmware on
`xiao-esp32s3-sense` and the companion app.

The API the phone calls on the owner's machine is still `POST /v1/process`
(see `PIPELINE.md` at the repo root). That machine is on the phone's LAN,
or otherwise reachable by the phone. It is not a cloud service the camera
knows about.

## Roles

1. Camera captures a JPEG, writes it to the microSD, and shows it locally
   if no phone is paired.
2. Camera sends a small preview over BLE, then a SoftAP session.
3. Phone joins the SoftAP and downloads the original with HTTP Range.
4. Phone posts that JPEG to the local API with the style id.
5. Phone uploads the edited JPEG back to the camera with HTTP PUT.
6. Camera stores the pair and opens the review screen if the shot is
   still in progress. If the user already left that screen, the file is
   still on the card for the storage browser.

## BLE

Device name: `Somnium`.

The phone should request an MTU of at least 185. Status and session values
are longer than the default 20-byte notify, so **read** those
characteristics (long reads work at the default MTU). Notifies are a poke
to read. Preview packets are 19 bytes so they notify at the default MTU.

Service `6f4a0001-8c3e-4b7d-9a1e-5d2c7b6e1040`

| Characteristic | UUID | Properties | Payload |
| --- | --- | --- | --- |
| Status | `6f4a0002-8c3e-4b7d-9a1e-5d2c7b6e1040` | read, notify | ASCII line below |
| Control | `6f4a0003-8c3e-4b7d-9a1e-5d2c7b6e1040` | write | one ASCII command |
| Preview | `6f4a0004-8c3e-4b7d-9a1e-5d2c7b6e1040` | notify | 19-byte binary packets |
| Session | `6f4a0005-8c3e-4b7d-9a1e-5d2c7b6e1040` | read, notify | ASCII line below |

### Status

```
paired=0 conn=1 queue=2 batt=? chg=? mode=anime preview=1a2b3c4d,2400,deadbeef
```

- `paired` is 1 after the person at the camera accepts this phone.
- `queue` is shots on the card that do not yet have a dream JPEG.
- `batt` is the smoothed pack percent, 0 to 100, from 3.3 V = 0% to
  4.2 V = 100%. `mv` is the smoothed BAT+ millivolts (pin reading times
  2). Either field is `?` until the first quiet sample. The camera does
  not sample during a capture, an SD write, or while the SoftAP is up,
  so a notify in those windows repeats the last number.
- `chg` stays `?`. There is no VBUS-sense GPIO. The charger LED is on
  the XIAO. USB charging holds BAT+ at the charger voltage, so `batt`
  reads high until the cell is actually near 4.2 V. Do not treat `batt`
  as a charge-state flag.

### Battery Service

In addition to the status line, the camera advertises the standard
Battery Service.

| | UUID | Properties |
| --- | --- | --- |
| Battery Service | `0x180F` | primary service |
| Battery Level | `0x2A19` | read, notify |

The value is one uint8, the same percent as `batt=`. Notifies follow the
status characteristic, about every 2 seconds while a phone is connected,
and only after a real sample exists. `0` means empty, not "unknown";
unknown is the absence of a value before the first sample.
- `mode` is the style id (`popcomic`, `watercolor`, `anime`, `surprise`,
  `pencil`).
- `preview` is `-` or `id,length,crc32` for the preview currently being
  notified. CRC32 is the ESP ROM / zlib CRC of the preview JPEG only.

### Control

Write one command, optional trailing newline. Verbs are case-insensitive.

| Command | When it works | Effect |
| --- | --- | --- |
| `PAIR` | connected, not yet accepted | Asks the camera to show the pair hint. The person still has to hold the shutter or click select. |
| `SHUTTER` | paired | Same as a shutter tap. |
| `SELECT` | paired | Same as a select click (storage, back, dismiss). |
| `MODE anime` | paired | Selects that style id. |
| `PULL 1a2b3c4d` | paired | Asks the camera to raise the SoftAP for this photo id (8 hex digits). |
| `TIME 1710000000` | paired | Unix seconds. Sets the clock used on the storage screen. |
| `FORGET` | paired | Drops the stored phone address. The next connection must be accepted again. |

Anything except `PAIR` is ignored until the camera has accepted this
connection. The camera stores one peer address. Accepting a new phone
replaces it.

### Pairing

1. Phone connects. Status shows `paired=0`. The camera screen says to
   hold the shutter or click select.
2. Person does that. Status flips to `paired=1`. The address is stored
   in NVS (`somnium` / `peer`).
3. Later connections from that address are paired immediately.
4. `FORGET`, or accepting a different phone, clears it.

No passkey and no LESC. The accept gesture is the authentication. Range
is whoever can see the BLE advertisement, until someone at the camera
accepts them.

### Preview packets

Each notify is exactly 19 bytes:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | u8 | bit0 = first byte of the JPEG, bit1 = last packet |
| 1 | u16 le | sequence, starting at 0. Payload bytes are `seq * 11`. |
| 3 | u32 le | photo id |
| 7 | u8 | number of valid JPEG bytes in this packet (1…11) |
| 8 | 11 | JPEG bytes, zero padded |

The preview is a small JPEG (about 120×68), not the full capture. Length
and CRC come from the status line. If a sequence number is missing, drop
the preview and download the original over HTTP. The preview is a hint,
not the archive.

Light sleep stops BLE. The phone should reconnect and read status again.

## SoftAP and HTTP

The camera raises an access point only while a transfer is needed, and
tears it down about 60 s after the last phone and the last byte
(`kSoftApIdleMs`), unless a shot is still waiting for its dream.

| Field | Value |
| --- | --- |
| SSID | `Somnium-` + 4 hex characters (the suffix is in the session line) |
| Password | 8 characters, new every time the AP is started, from the session line |
| IP | `192.168.4.1` |
| Port | 80 |
| Security | WPA2, one station |

Session characteristic (read it; the notify may be truncated):

```
AP A1B2 23456789 id=1a2b3c4d host=192.168.4.1 port=80
```

`A1B2` is the SSID suffix, so the SSID is `Somnium-A1B2`. The password is
the next token. `id` is the photo to pull. The camera notifies this
**before** it starts the AP. Wait about a second (`wait` is not a separate
field; one second is enough) so the beacons are up, then join. BLE may
drop when the phone changes Wi-Fi. That is fine: the HTTP port does not
need BLE, and the phone can reconnect BLE afterwards.

### `GET /v1/health`

`200` and body `ok`.

### `GET /v1/queue`

`text/plain`, one shot per line:

```
id=1a2b3c4d style=anime crc=89abcdef bytes=120034 dream=0 acked=0
```

`crc` is the original JPEG. `dream=0` means the phone has not PUT an edit
yet. Shots survive reboot (metadata under `/somnium/queue/<id>.meta`,
JPEGs under `/somnium/`).

### `GET /v1/photos/<id>/original.jpg`

Full response is `200` with `Accept-Ranges: bytes`. A `Range` header
returns `206` with `Content-Range`. Example:

```
GET /v1/photos/1a2b3c4d/original.jpg HTTP/1.1
Range: bytes=0-8191
```

```
HTTP/1.1 206 Partial Content
Accept-Ranges: bytes
Content-Type: image/jpeg
Content-Range: bytes 0-8191/120034
Content-Length: 8192
Connection: close
```

`Range: bytes=8192-` means from that offset through the end. The camera
closes the connection at the end of the range. To resume, open a new
connection and ask for the next gap. Check the JPEG CRC from the queue
list when the bytes are complete.

`GET /v1/photos/<id>/dream.jpg` is the same shape, for an edit that is
already on the card.

### `PUT /v1/photos/<id>/dream.jpg`

Body is the edited JPEG. `Content-Length` is required and must be at least
4 and at most 2500000. The camera checks JPEG SOI and EOI. `201` and body
`ok` on success. `400` if the body is not a JPEG or the id is unknown.

The phone should PUT only after the local API returns the edit. Style,
venue, and the process token stay on the phone; the camera already stored
the style id in the queue line.

### `POST /v1/photos/<id>/ack`

Body empty. Marks the queue entry acknowledged so a later UI can hide it.
The files stay on the card either way.

## Phone sequence

1. Scan BLE, connect, request MTU 185, subscribe to status, read it.
2. If `paired=0`, write `PAIR` and wait for the person at the camera.
   Poll status until `paired=1`.
3. Optionally `TIME <unix>` and `MODE <id>`.
4. On a new shot, status `preview=` fills in. Reassemble notify packets.
   Show that JPEG immediately.
5. Write `PULL <id>` if the camera has not already raised the AP (a new
   capture raises it on its own when a phone is paired).
6. Read the session characteristic. Join `Somnium-<suffix>` with that
   password. `GET /v1/health`, then download the original in whatever
   chunk size the radio likes. On a drop, `GET` the queue again and
   resume with `Range`.
7. `POST` the original to the local `/v1/process` with the `style` from
   the queue line. Use the same multipart fields the laptop bridge uses
   (`image`, `style`, and lat/lon if the phone has them).
8. `PUT` the response body to `/v1/photos/<id>/dream.jpg`.
9. `POST /v1/photos/<id>/ack`. Leave the AP. BLE can reconnect for the
   next shot.

If status says the camera is not connected, shots still sit in the queue.
`PULL` works for an old id; the camera does not require the shutter to
have just been pressed.

## What the camera still saves

```
/somnium/<style>_<n>_0_orig.jpg
/somnium/<style>_<n>_1_dream.jpg
/somnium/queue/<id>.meta
```

The storage screen on the camera browses the JPEGs. The meta file is for
the phone after a reboot.

## Timeouts

The camera waits up to `kCookTimeoutMs` (300 s) for the dream PUT after it
has started the AP for a new shot. That matches the old cook budget. If
it expires, the original stays queued and the screen shows the original
as saved. A later `PULL` still works.
