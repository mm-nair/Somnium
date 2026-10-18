# Several cameras, one server

One laptop can cook for more than one board. Each cook names a device and, optionally, a dream board. The shot JSON records both, and the existing gallery uploader posts that JSON unchanged as the `meta` part.

Shots with no device id behave as before: venue coordinates, the requested style, `device_id` and `board` null.

## Wire protocol

USB, optional tokens on the COOK header. Omit them and the line matches older firmware.

```
COOK style=anime len=18420 id=a1b2c3d4 crc=1a2b3c4d lat=47.65530 lon=-122.30350 device=cam-kitchen board=table
<jpeg bytes>
COOK_END
```

`device` and `board` are one token each: letters, digits, `.`, `_`, `-`, up to 64 characters. The bridge ignores a bad token and still cooks. It forwards them as form fields `device_id` and `board` on `POST /v1/process`.

The same fields can be sent as headers `X-Somnium-Device` and `X-Somnium-Board`. The form value wins.

Firmware hook, empty by default so a flashed board does not change the header until someone sets it:

```cpp
constexpr char kDeviceId[] = "";
constexpr char kDreamBoard[] = "";
```

Those live in `lib/Somnium/Config.h`. `Host::cook` appends `device=` and `board=` only when the token is valid. `App.cpp` passes the constants. A null style id now falls back to `popcomic` (the old fallback string was `cartoon`, which is not a mode).

## Per-device settings

`server/devices.example.json` is the seed. The running copy is `server/devices.json`, created on the first registration or cook that names a device. It is gitignored. Delete it to load the example again.

```json
{
  "name": "Kitchen",
  "default_style": "watercolor",
  "enabled_styles": ["watercolor", "pencil", "anime"],
  "default_board": "table",
  "location": {
    "lat": 47.6553,
    "lon": -122.3035,
    "label": "Kitchen",
    "override": true
  }
}
```

- Empty `enabled_styles` means every enabled server style is allowed.
- A requested style outside `enabled_styles` is replaced with `default_style` when that style is allowed, otherwise the first allowed style that exists. The JSON records both `style_id` and `style_requested`.
- `location.override` true (the default when lat/lon are set) replaces the coordinates on the cook. The camera still sends the compiled venue pair; the server uses the device pair instead. Set `override` to false to treat the device location as a fallback only.
- `location.label` becomes the context place name for that device. Built-in prompts do not print it. A user style that uses `{place}` will.
- An unknown device id is registered on first cook with blank settings, then shows up in `GET /v1/devices`.
- `default_board` is used when the cook does not name a board.

## Dream boards

A board is a named collection. Any device can post to it. Membership is the union of the registry list and the devices that have actually posted.

Shots stay files in `server/outputs/`:

```
<stamp>_<style>_0_orig.jpg
<stamp>_<style>_1_dream.jpg
<stamp>_<style>.json
```

The JSON gains:

```json
{
  "device_id": "cam-kitchen",
  "board": "table",
  "style_id": "watercolor",
  "style_requested": "watercolor"
}
```

`server/scripts/somnium_uploader.py` already uploads that file as `meta`. The gallery can read `device_id` and `board` without a new multipart field.

## HTTP

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/v1/devices` | Registered cameras |
| GET | `/v1/devices/{id}` | One camera |
| POST | `/v1/devices` | Create or replace. Body: `id`, plus the fields above |
| PATCH | `/v1/devices/{id}` | Update one camera |
| GET | `/v1/boards` | Boards, members, shot counts |
| POST | `/v1/boards` | Create. Body: `id`, `name`, `members` |
| GET | `/v1/boards/{id}` | One board, including its shots |
| GET | `/v1/boards/{id}/shots` | Shots only |

There is no auth. The server is the laptop on the USB/local network.

## Not done

- The gallery site does not filter by device or board yet. The metadata is in the upload.
- Cameras do not pick a board from the on-screen UI. Set `kDreamBoard` at flash time, `default_board` on the server, or pass `board` from the bridge.
- Wi-Fi, BLE, and the offline queue are other tracks.
- Two cooks in the same microsecond can still share a filename stem. The stamp includes microseconds; device id is in the JSON rather than the filename so existing slideshow pairing stays `<stem>_0_orig` / `<stem>_1_dream`.
