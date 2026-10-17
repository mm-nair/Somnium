# Project Somnium

ESP32-S3 edge client for a hackathon AI camera. Hardware library + bring-up firmware only (no UI / no API yet).

## Pins

| LCD | Encoder | Shutter |
| --- | --- | --- |
| SCK 21, MOSI 47, DC 14, CS 41, RST 48 | CLK 1, DT 2, SW 42 | GPIO 3 |

## Build

```sh
nix develop
pio run -t upload --upload-port /dev/ttyACM0
```

Optional Wi-Fi: copy `include/wifi_credentials.example.h` → `include/wifi_credentials.h`.

## Tests

```sh
nix develop

# live button / encoder feed
python tests/monitor_inputs.py
python tests/monitor_inputs.py --port /dev/ttyACM0

# capture → LCD, also save JPEG locally
python tests/capture_photo.py
python tests/capture_photo.py -o shot.jpg --width 320
```

Serial protocol (921600 baud): `hello` → `OK somnium …`; `capture [w] [q]` → JPEG framed by `PHOTO` / `PHOTO_END`. Physical shutter release also captures to the LCD.

## Layout

```
lib/Somnium/     hardware (Config, Camera, Display, Inputs, Wifi, Host, …)
lib/SomniumUi/   page UI toolkit (not Somnium screens yet)
src/main.cpp     event loop + EVT lines
tests/           monitor_inputs.py, capture_photo.py
```

## UI toolkit (`SomniumUi`)

Generic building blocks for later screens (loading, capture, gallery, …):

| Piece | Role |
| --- | --- |
| `Ui` | binds to the LCD, `tick()` → update + dirty draw |
| `Navigator` | page stack (`push` / `replace` / `pop`) |
| `Transition` | `None`, `FadeThrough`, `SlideLeft`, `SlideRight` |
| `Page` | `onEnter` / `onExit` / `update` / `draw` / `handleAction` |
| `Canvas` | clipped/origin-aware fill, lines, RGB565 blit |
| `Text` | measure, align, wrap (GFX 6×8 font) |
| `Tween` / `Easing` | short property animations |
| `Theme` | colors + spacing tokens |

```cpp
#include <SomniumUi.h>

using namespace Somnium::Ui;

struct BlankPage : Page {
  void draw(Canvas &c) override {
    c.clear(Ui::theme().bg);
    Text::drawIn(c, "Hello", c.bounds(),
                 {Ui::theme().text, Ui::theme().textMd, Align::Center,
                  VAlign::Middle});
  }
};

BlankPage page;
void setup() {
  Somnium::begin();
  Ui::begin();
  Ui::push(&page);
}
void loop() { Ui::tick(); }
```

Efficiency: redraw only when a page is dirty or a transition is running; slides use clip+origin; fade-through is a cheap solid wipe via the theme background (no full-frame blend buffer).
