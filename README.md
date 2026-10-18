# Somnium

![The Somnium camera](gallery_1.jpg)

A handheld camera. You pick a look, press the shutter, and the board sends the JPEG to a laptop over USB. The laptop reads the weather, writes an edit for that look, and asks Grok Imagine to repaint the shot. The edited JPEG comes back down the same cable onto a 320×240 screen. The original and the edit both stay on the SD card.

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

The same port still takes operator commands: `hello`, `capture`, `cooktest`, `sdlist`, `sdpull`, `sdwipe`, and `TIME <epoch>` to set the clock.

## Phone link

With `SOMNIUM_BLE=1` (the default in `platformio.ini`) the board also advertises as **Somnium** over Bluetooth LE. A phone can list the style presets, switch the selected one, press the shutter, and subscribe to status. When a dream comes back over USB, the camera notifies and then sends a 40×30 RGB565 preview in framed chunks.

The phone does not replace the laptop. Shutter and style writes are posted into the same input queue as the button and the dial, and the cook is still the USB `COOK` / `DREAM` exchange. BLE never writes that serial port. Set `-DSOMNIUM_BLE=0` for a USB-only build.

The radio stack is NimBLE-Arduino 1.4.3 rather than Bluedroid: this core is Arduino-ESP32 2.0.17, and NimBLE keeps the host smaller in flash and RAM and can put host buffers in PSRAM, next to the JPEG buffers. The GATT layout, the chunk framing, and the Chrome companion page are in [tools/ble/README.md](tools/ble/README.md).

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

- `lib/Somnium` — camera, USB, SD, buttons, display
- `lib/SomniumUi` — pages, canvas, text, tweens
- `lib/SomniumApp` — the five screens, and the generated mode and theme headers
- `server/` — the FastAPI pipeline
- `scripts/usb_bridge.py` — serial to `/v1/process`

## Run

Nix, the ESP32-S3 on USB, and an xAI key.

```sh
nix develop
cp .env.example .env     # XAI_KEY=...

./scripts/flash.sh
./scripts/server.sh      # leave this running
./scripts/usb_bridge.sh  # only while shooting
```

Stop the bridge before flashing or before `./scripts/usb_pull_sd.sh`, which copies `/somnium` into `demos/sd_<timestamp>/`. `./scripts/dev.sh` prints the same checklist.

Offline, with no board attached:

```sh
nix develop -c pio run
nix develop -c python -m unittest discover -s tests -p 'test_*.py' -v
```

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

