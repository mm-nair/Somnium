# Project Somnium

DIY reality-bending AI camera for a 36-hour hackathon. This repo is the **ESP32-S3 edge client**: capture a JPEG, show it on the rear SPI LCD, read the encoder/shutter, and keep Wi-Fi/serial ready for a later Stable Diffusion backend.

Current scope: hardware bring-up library + thin firmware. **No UI chrome and no API client yet.**

## Hardware

| Piece | Notes |
| --- | --- |
| Controller | Freenove ESP32-S3 WROOM CAM (FNK0085, N16R8 — 16 MB flash, 8 MB OPI PSRAM) |
| Sensor | Board camera (this kit uses **GC0308**; RGB565 + software JPEG) |
| Display | 2.4" SPI TFT (ILI9341-compatible) |
| Controls | Rotary encoder (turn + press) + separate shutter button |

### Pin map (wiring diagram)

| Function | GPIO |
| --- | --- |
| LCD SCK | 21 |
| LCD MOSI | 47 |
| LCD DC | 14 |
| LCD CS | 41 |
| LCD RST | 48 |
| Encoder CLK | 1 |
| Encoder DT | 2 |
| Encoder SW | 42 |
| Shutter | 3 |

Inputs are active LOW with `INPUT_PULLUP`. LCD VCC/LED and encoder `+` are on the 3V3 rail (LED via 100 Ω). Camera DVP pins are fixed by the Freenove board (see `lib/Somnium/Config.h`).

> The overview text mentioned four discrete buttons on GPIOs 1/2/42/45. The physical diagram uses an encoder on 1/2/42 and shutter on 3 — firmware follows the diagram.

## Layout

```
Somnium/
  platformio.ini          # espressif32 + Freenove N16R8 overrides
  flake.nix               # platformio + pyserial/pillow shell
  include/                # wifi_credentials.example.h (copy → wifi_credentials.h)
  lib/Somnium/            # unified hardware library
    Config.h              # pins + defaults
    Console.*             # USB-UART logging
    Wifi.*                # STA connect / poll
    Inputs.*              # debounced shutter + quadrature encoder
    Camera.*              # esp_camera capture → JPEG
    Display.*             # ILI9341 blit (no UI)
    Image.*               # takePhoto / JPEG helpers (+ drawJpeg stub)
    Somnium.h             # umbrella + Somnium::begin()
  src/main.cpp            # bring-up loop using the library
```

Build the full product by extending `lib/Somnium` (HTTP client, style presets, gallery) and keeping `src/main.cpp` thin.

## Build & flash

```sh
nix develop
cp include/wifi_credentials.example.h include/wifi_credentials.h
# edit credentials — 2.4 GHz only

pio run -t upload --upload-port /dev/ttyACM0
pio device monitor --port /dev/ttyACM0
```

Use the board’s **USB-to-UART** connector (CH343), not native USB OTG. Stable by-id path from the prep board often looks like `/dev/serial/by-id/usb-1a86_USB_Single_Serial_*-if00`.

## Firmware behavior

1. Serial banner + pin dump  
2. Init encoder/shutter, LCD (black), camera, Wi-Fi (background if credentials present)  
3. **Shutter release** → capture JPEG, draw RGB565 frame on LCD, log size/CRC  
4. **Encoder turn** → cycle a style index (logged only; no API yet)  
5. Encoder press/release logged for later mode binding  

`Image::drawJpeg()` is intentionally a stub for future stylized JPEG replies.

## Library quick reference

```cpp
#include <Somnium.h>

void setup() {
  Somnium::begin();
}

void loop() {
  Somnium::Wifi::poll();
  Somnium::Inputs::update();
  if (Somnium::Inputs::pollEvent() == Somnium::InputEvent::ShutterReleased) {
    Somnium::JpegBuffer photo;
    if (Somnium::Image::takePhoto(photo)) {
      // photo.data / photo.length ready for HTTP POST later
      Somnium::Image::freePhoto(photo);
    }
  }
}
```

## Reference

Working patterns (camera RGB565→JPEG, Adafruit ILI9341, PlatformIO N16R8 flags) were adapted from `~/Projects/Hackathon_Prep`, reorganized into one library instead of scattered examples.
