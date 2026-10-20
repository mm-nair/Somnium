# Somnium

![The Somnium camera](gallery_1.jpg)

A handheld camera. You pick a look, press the shutter, and the board sends the JPEG to a laptop. On Wi-Fi it POSTs the shot straight to the FastAPI server. If the network is down, it falls back to the USB serial link. The laptop reads the weather, writes an edit for that look, and asks Grok Imagine to repaint the shot. The edited JPEG comes back onto a 320×240 screen. The original and the edit both stay on the SD card.

*Somnium* is Latin for "the dream." The word is also the title of Johannes Kepler's *Somnium* (1634), often called the first science-fiction novel, a dream of a voyage to the Moon and of the Earth seen from there.

Photos from the camera are posted at [somnium.photo](https://somnium.photo). The site was built with Cursor and Grok Bot.

## In the hand

The firmware screens are Boot, Modes, Process, Review, and Storage.

Modes is a carousel: Comic, Watercolor, Anime, Surprise, and Pencil Sketch. The card art is compiled in from `creative/previews`. The shutter takes a photo in the selected mode from any screen. Process is the wait: a flame, a salt shaker, and a whisk, redrawn while the laptop works. Review holds the edit for a few seconds. Click the dial to open Storage. Turning it there steps through saved shots and flips each pair between the raw photo and the edit, labeled `ANIME-raw` and `ANIME-mod`.

A shot is two files:

```
/somnium/anime_3_0_orig.jpg
/somnium/anime_3_1_dream.jpg
```

The next shot number skips filenames already on the card, so a reboot leaves the existing pairs alone. If the SD write fails, the same pair is kept in RAM and still shows up in Storage.

## USB serial

The laptop link is the board's USB serial port: the onboard CH340, UART0, 921600 baud. Power, logs, and both JPEGs use that cable.

The camera opens a cook with a text header, the JPEG, and a trailer. `id` is a random request id. `crc` is the CRC32 of the JPEG bytes (`esp_rom_crc32_le`, the same value as Python `zlib.crc32`).

```
COOK style=anime len=18420 id=a1b2c3d4 crc=1a2b3c4d lat=47.65530 lon=-122.30350
<jpeg bytes>
COOK_END
```

`scripts/usb_bridge.py` reads that frame and POSTs it to `http://127.0.0.1:8080/v1/process`. When the edit comes back, the bridge sends the length and checksum first and waits:

```
DREAM a1b2c3d4 16200 5e6f7081
```

The ESP allocates a buffer and answers `READY DREAM a1b2c3d4`. Only then does the bridge send the JPEG and `DREAM_END`. The camera checks the trailer, the CRC, and the JPEG markers, then replies `ACK DREAM a1b2c3d4`. The bridge reports success after that ACK.

A disconnect abandons the in-flight request. The next connection starts clean. Firmware and the bridge implement the same handshake, so they are updated together.

Two things that made the transfer reliable: the serial receive buffer is 64 KB, because a dream JPEG is tens of kilobytes and the default 256-byte buffer overflowed while the panel was still refreshing. And the UI keeps animating during the wait, then stops once the DREAM header arrives, so SPI drawing and the incoming JPEG are not interleaved.

The same port still takes operator commands: `hello`, `capture`, `cooktest`, `sdlist`, `sdpull`, `sdwipe`, and `TIME <epoch>` to set the clock. Wi-Fi adds `link`, `portal`, `wificlear`, and `wificook`. `cooktest` stays on USB. `wificook` uses Wi-Fi first and falls back to USB.

## Wi-Fi

The ESP32-S3 radio is 2.4 GHz only. A 5 GHz venue network, or an iPhone hotspot that isolates clients, still cannot carry the cook. Use a 2.4 GHz LAN where the laptop and the camera can see each other (a travel router or an Android hotspot works; the laptop has to be on that same network, not only on USB).

The camera stores the network and the server in NVS (`Preferences`, namespace `somnium`). Compile-time defaults are optional: copy `include/wifi_credentials.example.h` and `include/server_config.example.h` to the gitignored `wifi_credentials.h` and `server_config.h`. Empty strings boot the setup portal instead. A portal save wins over the compile-time file until the serial command `wificlear` or a full flash erase.

Hold the shutter and the dial together for 1.5 seconds to open the portal. Do not hold the dial while resetting or flashing: GPIO 0 is also BOOT. The camera starts a SoftAP named `Somnium-Setup` (open, 192.168.4.1) and a one-page form for the SSID, password, server host, port, path, and an optional gallery URL and bearer token. The token is written to NVS on the camera and is not printed on serial. Nothing in git should contain a real token.

The server host can be an IP, a URL, or `somnium.local`. `./scripts/server.sh` advertises `somnium.local` and `_somnium._tcp` on mDNS. Set `SOMNIUM_MDNS=0` to skip that, or `SOMNIUM_ADVERTISE_IP` if the guessed LAN address is wrong. If UDP 5353 is already taken, put the laptop IP in the portal.

A cook checks `GET /health`, then `POST /v1/process` as multipart JPEG with `style`, `taken_at`, `lat`, `lon`, and `source=wifi`. Connect failures and HTTP 408/429/5xx retry with backoff (four tries). A 4xx other than those does not retry. There is no offline queue: if Wi-Fi still fails, the same JPEG is sent with the USB `COOK` frame. The header shows `Setup`, `WiFi*`, or `WiFi.`. The process line shows `wifi > server...`, `wifi retry N...`, `gallery...`, or `usb > thinkpad...`.

When a gallery URL and bearer token are stored, the camera also POSTs `orig`, `dream`, and `meta` to that URL (`Authorization: Bearer` and `X-Somnium-Token`) and tells the laptop to skip its own gallery hook for that shot. Leave the laptop `SOMNIUM_UPLOAD_TOKEN` unset in that case. Gallery TLS does not pin a CA yet (`setInsecure`). The dream JPEG still returns if the gallery POST fails.

## Photo processing

The server is a FastAPI app with one endpoint, `POST /v1/process`. The body is multipart: `image`, `style`, `taken_at`, and optional `lat` / `lon`. The response body is the edited JPEG. Headers carry the style, a short prompt preview, and the filenames that were saved.

**Where and when.** Open-Meteo supplies weather, cloud cover, wind, daylight, sunrise, and sunset for the lat/lon on the cook. That coordinate is the venue in `server/settings.json`, and the same pair is compiled into the firmware. Moon phase is computed on the laptop. A short list of bright-sky names is a season and hour heuristic. This step uses no API key. If Open-Meteo fails, a stub context is filled in and the edit still runs.

**Light, not weather as subject.** `mood_artifacts` in `creative/creative.json` turns the weather and the time of day into two short cues: softer shadow edges, a cooler fill, warmer highlights. The prompt writer folds those into one sentence about light on surfaces already in the frame. Moon phase and the other sky notes are saved on the shot and left out of the prompt. Longer mood language made the image model paint the weather in as new scenery.

**The instruction.** Each mode has a draft in `creative.json`. The draft names the medium (ink and halftone, watercolor, cel shading, colored pencil) and tells the model to keep the people, the number of objects, the pose, the crop, and the room. `grok-3-mini` rewrites that draft into one edit, roughly 100 to 160 words. Surprise has no fixed medium. The server picks a treatment, a palette, and a focus from the lists on that mode, skipping the combination it used last time, and the prompt has to follow the pick. A film treatment stays a photograph.

**The edit.** `grok-imagine-image-2.0` takes the photo and the prompt on `/v1/images/edits`. A short constraint is appended: same crop, same setting, no added text. The model returns a larger image. The server scales it back to the camera frame with Lanczos and writes JPEG at quality 85. On this board that frame is 320×240, which is what goes back over USB and onto the card.

Each shot also leaves three files in `server/outputs/`: the original, the dream, and a JSON sidecar with the full prompt and context. The HTTP prompt header is plain ASCII and truncated. A generated prompt with a stray character was rejected by the HTTP stack after the image had already been made, so the header is a preview and the JSON is the record.

The same pipeline runs without the camera:

```sh
./scripts/cook.sh photo.jpg --style watercolor
```

## The board and the screen

![Circuit diagram](gallery_2.jpg)

Freenove ESP32-S3 WROOM CAM, 16 MB flash, octal PSRAM. An ILI9341 320×240 on SPI at 10 MHz, a rotary encoder, a shutter button, and a microSD card. The enclosure is `cad/Front Panel.stl` and `cad/Back Panel.stl`.

LCD pins are SCK 1, MOSI 2, CS 21, DC 14, RST 47. Encoder CLK 42, DT 41, switch on GPIO 0. Shutter on GPIO 3. GPIO 0 is also BOOT, so the knob has to be released while resetting or flashing. On boot the panel flashes red, green, blue, then black.

The sensor is mounted in portrait. Before the JPEG is encoded, firmware crops a square from the frame, rotates it to landscape 320×240, and mirrors it horizontally. The mirror flag is `Defaults::kCameraMirrorHorizontal` in `lib/Somnium/Config.h`. The sensor is powered down for the USB wait. Leaving it running through the edit was dropping the CH340 off the bus.

The panel decodes JPEGs with TJpg_Decoder, native RGB565, byte swap off. The camera library exports its own `jd_prepare` and `jd_decomp`, with a different struct and pixel format. Linking that decoder in produced corrupt frames. Display code stays on TJpg_Decoder, and a test reads the firmware map to confirm both symbols resolve there.

A full panel is 76,800 pixels. Flushing all of them at 10 MHz was too slow for the carousel, so the canvas keeps the previous frame in PSRAM and sends the runs that changed. If that buffer cannot be allocated, it sends the whole frame. Every screen draws through that canvas, so the saved frame matches what was last shown.

On-screen text is Adafruit GFX, ASCII. Colors, the logo, mode names, thumbnails, and prompt drafts live in `creative/`. `./scripts/sync_creative.sh` regenerates the firmware headers, including the 138×82 mode thumbnails. The Python server reads `creative.json` on startup, so a prompt edit is a server restart. A color, font, logo, or thumbnail edit is a sync, a rebuild, and a flash.

`./scripts/ui_preview.sh` draws the same screens in a window on the laptop.

Code is split like this:

- `lib/Somnium` — camera, Wi-Fi, USB, SD, buttons, display
- `lib/SomniumUi` — pages, canvas, text, tweens
- `lib/SomniumApp` — the five screens, and the generated mode and theme headers
- `server/` — the FastAPI pipeline and mDNS advertiser
- `scripts/usb_bridge.py` — serial fallback to `/v1/process`

## Run

Nix, the ESP32-S3 on USB, and an xAI key.

```sh
nix develop
cp .env.example .env     # XAI_KEY=...

./scripts/flash.sh
./scripts/server.sh      # leave this running; laptops and cameras share the LAN
./scripts/usb_bridge.sh  # only when the camera is falling back to USB
```

On first boot with no saved network, join `Somnium-Setup` from a phone and submit the form. Stop the bridge before flashing or before `./scripts/usb_pull_sd.sh`, which copies `/somnium` into `demos/sd_<timestamp>/`. `./scripts/dev.sh` prints the same checklist.

Offline, with no board attached:

```sh
nix develop -c pio run
nix develop -c python -m unittest discover -s tests -p 'test_*.py' -v
```

## How to test Wi-Fi

On the camera, after a flash:

1. Laptop and camera on the same 2.4 GHz LAN. `./scripts/server.sh` stays up.
2. If the header says `Setup`, join `Somnium-Setup` and save the form. Or hold shutter and dial for 1.5 seconds.
3. Serial (`link`) should show `phase=WiFi*` and an IP. `GET http://somnium.local:8080/health` from the laptop should match.
4. Shutter. The process line should read `wifi > server...`, then Review. `server/outputs/` gains the orig, dream, and JSON sidecar with `"source": "wifi"`.
5. Unplug the laptop from that LAN, or stop the server, and shoot again with the USB bridge running. The line should switch to `usb > thinkpad...` and the dream should still arrive.
6. `wificook` on serial does the same cook without the shutter. `portal` reopens setup. `wificlear` drops NVS and returns to compile-time defaults.

Without a camera:

```sh
nix develop -c pio run
nix develop -c python -m unittest discover -s tests -p 'test_*.py' -v
./scripts/server.sh          # other terminal
./scripts/wifi_test.sh       # health, mDNS name, LAN IP
```

`tests/test_link_plan.py` compiles the portal, retry, and multipart decisions. `tests/test_wifi_upload.py` POSTs a camera-shaped multipart body at `/v1/process` and checks the dream JPEG. `tests/test_mdns_advertiser.py` checks the `somnium.local` packet. None of those need a board or an xAI key.

Wire format: [PIPELINE.md](https://github.com/mm-nair/Somnium/blob/main/PIPELINE.md). Editing modes and prompts: [creative/README.md](https://github.com/mm-nair/Somnium/blob/main/creative/README.md).

## Built with

ESP32-S3, PlatformIO, FastAPI, xAI Grok, Open-Meteo, Nix. Public gallery: [somnium.photo](https://somnium.photo).

## Gallery upload

On the NixOS image server, shots are saved under `server/outputs/`. Uploading
them is optional. Source a mode `600` env file that stays out of git (see
[`server/README.md`](server/README.md)):

```sh
export SOMNIUM_UPLOAD_URL=https://www.somnium.photo/api/upload
export SOMNIUM_UPLOAD_TOKEN=your-token-here
```

`your-token-here` is a placeholder. When both variables are set for the camera
server, it uploads each saved shot and the directory watcher is optional. To
watch `server/outputs` instead, from the repository root:

```sh
nix develop -c python server/scripts/somnium_uploader.py watch server/outputs
nix run .#uploader
```

