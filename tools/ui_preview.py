#!/usr/bin/env python3
"""Somnium LCD preview — 320x240 panel simulator for the ThinkPad.

  nix develop
  python tools/ui_preview.py
  python tools/ui_preview.py --export /tmp/somnium-ui
  python tools/ui_preview.py --screen storage

Sidebar (or 1–9 / [ ] / Tab) picks a screen.
  Modes / Storage: ←/→   Review: auto-returns to Modes after ~2.2s
  Esc quits.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CREATIVE = ROOT / "creative" / "creative.json"
LOGO_H = ROOT / "lib" / "SomniumApp" / "LogoBitmap.h"

W, H = 320, 240
SCALE = 3
SIDEBAR_W = 200
REVIEW_MS = 2200

SCREENS = [
    ("boot", "Boot"),
    ("boot_mid", "Boot (mid)"),
    ("modes", "Modes"),
    ("modes_slide", "Modes (sliding)"),
    ("process", "Process"),
    ("review", "Review (post-shot)"),
    ("storage", "Storage"),
    ("storage_empty", "Storage (empty)"),
    ("wifi_off", "Modes · WiFi off"),
]

# Fake SD gallery for Storage browsing in the preview.
FAKE_PHOTOS = [
    {"label": "NOIR", "tint": (50, 50, 55)},
    {"label": "CARTOON", "tint": (90, 40, 70)},
    {"label": "CYBER", "tint": (20, 70, 90)},
    {"label": "WASH", "tint": (80, 55, 40)},
    {"label": "GOURMET", "tint": (70, 45, 30)},
]


def rgb565_to_rgb(c: int) -> tuple[int, int, int]:
    r = ((c >> 11) & 0x1F) * 255 // 31
    g = ((c >> 5) & 0x3F) * 255 // 63
    b = (c & 0x1F) * 255 // 31
    return r, g, b


def load_theme() -> dict:
    data = json.loads(CREATIVE.read_text())
    t = data["theme"]
    return {
        "bg": tuple(t["bg"]),
        "pink": tuple(t["pink"]),
        "pinkDim": tuple(t["pinkDim"]),
        "muted": tuple(t["muted"]),
        "surface": tuple(t["surface"]),
        "panel": tuple(t["panel"]),
        "modes": data["modes"],
    }


def load_logo() -> list[list[tuple[int, int, int]]] | None:
    if not LOGO_H.exists():
        return None
    text = LOGO_H.read_text()
    mw = re.search(r"kLogoW\s*=\s*(\d+)", text)
    mh = re.search(r"kLogoH\s*=\s*(\d+)", text)
    if not mw or not mh:
        return None
    lw, lh = int(mw.group(1)), int(mh.group(1))
    nums = [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+", text)]
    if len(nums) < lw * lh:
        return None
    pixels = nums[-lw * lh :]
    return [
        [rgb565_to_rgb(pixels[y * lw + x]) for x in range(lw)] for y in range(lh)
    ]


def ease_in_out_quad(t: float) -> float:
    t = max(0.0, min(1.0, t))
    return 2 * t * t if t < 0.5 else 1 - 2 * (1 - t) * (1 - t)


def boot_stage(ms: int) -> str:
    if ms < 1800:
        return "warming the oven..."
    if ms < 3600:
        return "whisking starlight..."
    if ms < 5400:
        return "salting the sensors..."
    if ms < 7200:
        return "proofing the pixels..."
    return "plating Somnium..."


class Preview:
    def __init__(self, theme: dict, logo: list | None):
        import pygame

        self.pygame = pygame
        pygame.init()
        pygame.display.set_caption("Somnium UI preview — pick a screen in the sidebar")
        self.win_w = SIDEBAR_W + W * SCALE
        self.win_h = H * SCALE
        self.screen = pygame.display.set_mode((self.win_w, self.win_h))
        self.fb = pygame.Surface((W, H))
        self.clock = pygame.time.Clock()
        self.font = pygame.font.SysFont("dejavuserif", 14, italic=True)
        self.font_sm = pygame.font.SysFont("dejavusans", 11)
        self.font_side = pygame.font.SysFont("dejavusans", 13)
        self.font_side_sm = pygame.font.SysFont("dejavusans", 11)
        self.font_lg = pygame.font.SysFont("dejavuserif", 18, italic=True)
        self.theme = theme
        self.logo = logo
        self.screen_i = 0
        self.page = SCREENS[0][0]
        self.boot_ms = 0
        self.boot_live = True
        self.index = 0
        self.slide = 0.0
        self.slide_from = 0.0
        self.slide_t0 = 0.0
        self.slide_dur = 0.36
        self.sliding = False
        self.process_ms = 0
        self.wifi_on = True
        self.photos = list(FAKE_PHOTOS)
        self.photo_i = len(self.photos) - 1  # newest
        self.review_ms = 0
        self.review_live = False
        self.running = True
        self._row_h = 28
        self._list_top = 36

    def fill(self, color):
        self.fb.fill(color)

    def rect(self, r, color, width=0):
        self.pygame.draw.rect(self.fb, color, r, width)

    def text(self, s, xy, color, font=None, center=False):
        font = font or self.font_sm
        surf = font.render(s, True, color)
        if center:
            xy = (xy[0] - surf.get_width() // 2, xy[1])
        self.fb.blit(surf, xy)

    def draw_logo(self, x, y):
        if not self.logo:
            self.text("Somnium", (x + 40, y + 20), self.theme["pink"], self.font_lg)
            return
        for row_y, row in enumerate(self.logo):
            for col, rgb in enumerate(row):
                if rgb != (0, 0, 0):
                    self.fb.set_at((x + col, y + row_y), rgb)

    def draw_fire(self, cx, cy, t_ms):
        pink = self.theme["pink"]
        bob = int(math.sin(t_ms / 90.0) * 2)
        self.rect((cx - 9, cy + 11, 18, 3), pink)
        self.pygame.draw.line(
            self.fb, pink, (cx - 7, cy + 9), (cx + 6, cy + 9)
        )
        self.rect((cx - 6, cy + 2 + bob, 12, 8), pink)
        self.rect((cx - 4, cy - 4 + bob, 8, 8), pink)
        self.rect((cx - 2, cy - 10 + bob, 4, 8), pink)
        self.rect((cx - 1, cy - 14 + bob, 2, 2), pink)
        self.rect((cx - 8, cy + 4 - bob, 2, 2), pink)
        self.rect((cx + 6, cy + 3 + bob, 2, 2), pink)

    def draw_salt(self, cx, cy, t_ms):
        pink = self.theme["pinkDim"]
        shake = int(math.sin(t_ms / 160.0) * 2)
        x = cx + shake
        self.rect((x - 6, cy - 2, 12, 16), pink)
        self.rect((x - 7, cy - 6, 14, 5), pink)
        self.rect((x - 2, cy - 9, 4, 3), pink)
        for hx, hy in ((-3, 2), (1, 2), (-3, 7), (1, 7)):
            self.rect((x + hx, cy + hy, 2, 2), (0, 0, 0))
        if (t_ms // 120) % 2 == 0:
            self.rect((x - 1, cy + 16, 2, 2), pink)
            self.rect((x + 3, cy + 18, 2, 2), pink)

    def draw_whisk(self, cx, cy, t_ms):
        pink = self.theme["pinkDim"]
        spin = t_ms / 200.0
        self.rect((cx - 1, cy + 5, 3, 14), pink)
        self.rect((cx - 1, cy + 3, 3, 3), pink)
        for loop in range(3):
            a0 = spin + loop * 0.55
            ca, sa = math.cos(a0), math.sin(a0)
            for t in range(10):
                u = t * (math.tau / 10)
                lx = math.cos(u) * 9.0
                ly = math.sin(u) * 5.5 - 1.0
                x = int(cx + lx * ca - ly * sa * 0.35)
                y = int(cy + lx * sa * 0.35 + ly * ca)
                self.rect((x, y, 2, 2), pink)

    def draw_pill(self, box, progress):
        x, y, w, h = box
        self.rect((x, y, w, h), self.theme["surface"])
        fill = int(w * max(0.0, min(1.0, progress)))
        if fill:
            self.rect((x, y, fill, h), self.theme["pink"])
        self.rect((x, y, w, h), self.theme["pink"], 1)

    def draw_header(self, left="Storage", wifi=True):
        self.rect((0, 0, W, 22), self.theme["surface"])
        self.text(left, (6, 5), self.theme["pink"])
        self.text(
            "WiFi*" if wifi else "WiFi.",
            (W // 2 - 18, 5),
            self.theme["muted"],
        )
        when = time.strftime("%b %d  %H:%M")
        surf = self.font_sm.render(when, True, self.theme["pink"])
        self.fb.blit(surf, (W - surf.get_width() - 6, 5))

    def fake_photo(self, photo: dict):
        tint = photo["tint"]
        for row in range(H):
            t = row / max(1, H - 1)
            r = int(tint[0] * (0.5 + 0.5 * t))
            g = int(tint[1] * (0.6 + 0.4 * (1 - t)))
            b = int(tint[2] * (0.5 + 0.5 * t))
            self.pygame.draw.line(self.fb, (r, g, b), (0, row), (W - 1, row))
        self.text(
            photo["label"],
            (W // 2, H // 2 - 8),
            self.theme["pink"],
            self.font,
            center=True,
        )

    def select_screen(self, i: int):
        i %= len(SCREENS)
        self.screen_i = i
        self.page = SCREENS[i][0]
        self.boot_live = self.page == "boot"
        self.wifi_on = self.page != "wifi_off"
        self.review_live = self.page == "review"
        self.review_ms = 0
        if self.page in ("boot", "boot_mid"):
            self.boot_ms = 0 if self.page == "boot" else 4500
        elif self.page in ("modes", "wifi_off"):
            self.index = 0
            self.slide = 0.0
            self.sliding = False
        elif self.page == "modes_slide":
            self.index = 1
            self.slide = 0.45
            self.sliding = False
        elif self.page == "process":
            self.process_ms = 0
        elif self.page == "storage":
            self.photos = list(FAKE_PHOTOS)
            self.photo_i = len(self.photos) - 1
        elif self.page == "storage_empty":
            self.photos = []
            self.photo_i = 0
        elif self.page == "review":
            self.photos = list(FAKE_PHOTOS)
            self.photo_i = len(self.photos) - 1

    def draw_boot(self, dt):
        if self.boot_live:
            self.boot_ms += int(dt * 1000)
            if self.boot_ms > 9000:
                self.boot_ms = 9000
        self.fill(self.theme["bg"])
        lw = len(self.logo[0]) if self.logo else 160
        self.draw_logo((W - lw) // 2, 28)
        # L→R: fire · salt · whisk
        self.draw_fire(72, 118, self.boot_ms)
        self.draw_salt(160, 108, self.boot_ms)
        self.draw_whisk(248, 110, self.boot_ms + 200)
        self.text(
            boot_stage(self.boot_ms),
            (W // 2, 168),
            self.theme["muted"],
            center=True,
        )
        self.draw_pill((40, 198, 240, 14), self.boot_ms / 9000.0)

    def _panel(self, mode_i, slot, slide):
        modes = self.theme["modes"]
        n = len(modes)
        mode = modes[mode_i % n]
        x_norm = slot + slide
        depth = abs(x_norm)
        scale = 1.0 - depth * 0.22
        w = int(150 * scale)
        h = int(150 * scale)
        cx = int(W / 2 + x_norm * 95)
        cy = int(118 + depth * 10)
        x, y = cx - w // 2, cy - h // 2
        front = depth < 0.35
        frame = self.theme["pink"] if front else self.theme["pinkDim"]
        self.rect((x, y, w, h), self.theme["panel"])
        self.rect((x, y, w, h), frame, 1)
        img_h = int(h * 0.55)
        tint = (
            40 + (abs(hash(mode["id"])) % 80),
            20 + (abs(hash(mode["label"])) % 60),
            40 + (abs(hash(mode["id"][::-1])) % 80),
        )
        self.rect((x + 6, y + 6, w - 12, img_h), tint)
        color = self.theme["pink"] if front else self.theme["muted"]
        font = self.font if front else self.font_sm
        self.text(mode["label"], (x + w // 2, y + h - 26), color, font, center=True)

    def nudge_modes(self, delta: int):
        if delta == 0 or self.sliding:
            return
        if self.page not in ("modes", "wifi_off"):
            return
        self.index += 1 if delta > 0 else -1
        self.slide_from = 1.0 if delta > 0 else -1.0
        self.slide = self.slide_from
        self.slide_t0 = time.perf_counter()
        self.sliding = True

    def nudge_storage(self, delta: int):
        n = len(self.photos)
        if n <= 0 or delta == 0:
            return
        self.photo_i = (self.photo_i + (1 if delta > 0 else -1)) % n

    def update_slide(self):
        if self.page == "modes_slide":
            return
        if not self.sliding:
            self.slide = 0.0
            return
        t = (time.perf_counter() - self.slide_t0) / self.slide_dur
        if t >= 1.0:
            self.slide = 0.0
            self.sliding = False
            return
        self.slide = self.slide_from + (0.0 - self.slide_from) * ease_in_out_quad(t)

    def draw_modes(self, _dt):
        self.update_slide()
        self.fill(self.theme["bg"])
        self.draw_header(wifi=self.wifi_on)
        self.rect((0, 22, W, H - 22), self.theme["bg"])
        for slot, off in ((-2, -2), (2, 2), (-1, -1), (1, 1), (0, 0)):
            self._panel(self.index + off, float(slot), self.slide)
        self.text(
            "turn dial · click shutter",
            (W // 2, 220),
            self.theme["muted"],
            center=True,
        )

    def draw_process(self, dt):
        self.process_ms += int(dt * 1000)
        mode = self.theme["modes"][self.index % len(self.theme["modes"])]
        self.fill(self.theme["bg"])
        self.text(
            "cam > plate > sparkle",
            (W // 2, 40),
            self.theme["muted"],
            center=True,
        )
        y = 88
        for x in (36, 140, 244):
            self.rect((x, y, 40, 40), self.theme["panel"])
        self.pygame.draw.line(self.fb, self.theme["pink"], (76, y + 20), (140, y + 20))
        self.pygame.draw.line(self.fb, self.theme["pink"], (180, y + 20), (244, y + 20))
        # Same order as Boot: fire · salt · whisk — no progress bar
        self.draw_fire(56, y + 14, self.process_ms)
        self.draw_salt(160, y + 12, self.process_ms)
        self.draw_whisk(264, y + 10, self.process_ms)
        self.text(
            "Magic is Happening...",
            (W // 2, 160),
            self.theme["pink"],
            self.font,
            center=True,
        )
        self.text(mode["label"], (W // 2, 196), self.theme["muted"], center=True)

    def draw_preview_chrome(self, kind: str, label: str, index1: int, total: int):
        self.rect((0, 0, W, 22), self.theme["surface"])
        if kind == "storage":
            self.text("< Modes", (6, 5), self.theme["pink"])
        else:
            self.text(label, (6, 5), self.theme["pink"])

        self.rect((0, H - 28, W, 28), self.theme["surface"])
        if kind == "storage":
            meta = "no plates yet" if total <= 0 else f"{label}  {index1}/{total}"
            self.text(meta, (8, H - 18), self.theme["pink"])
        else:
            self.text("saved", (8, H - 18), self.theme["muted"])

        when = time.strftime("%b %d  %H:%M")
        surf = self.font_sm.render(when, True, self.theme["pink"])
        tx = W - surf.get_width() - 8
        self.fb.blit(surf, (tx, H - 18))
        self.pygame.draw.line(
            self.fb, self.theme["pinkDim"], (tx, H - 8), (tx + surf.get_width(), H - 8)
        )

    def draw_review(self, dt):
        if self.review_live:
            self.review_ms += int(dt * 1000)
            if self.review_ms >= REVIEW_MS:
                # Camera-style: glance then back to Modes
                self.select_screen([s[0] for s in SCREENS].index("modes"))
                return
        mode = self.theme["modes"][self.index % len(self.theme["modes"])]
        photo = {"label": mode["label"], "tint": (60, 30, 50)}
        self.fake_photo(photo)
        self.draw_preview_chrome("review", mode["label"], 0, 0)

    def draw_storage(self, _dt):
        n = len(self.photos)
        if n <= 0:
            self.fill(self.theme["bg"])
            self.text("no plates yet", (W // 2, 110), self.theme["muted"], center=True)
            self.draw_preview_chrome("storage", "", 0, 0)
            return
        photo = self.photos[self.photo_i]
        self.fake_photo(photo)
        self.draw_preview_chrome("storage", photo["label"], self.photo_i + 1, n)

    def draw_panel_page(self, dt: float):
        if self.page in ("boot", "boot_mid"):
            self.draw_boot(dt)
        elif self.page in ("modes", "modes_slide", "wifi_off"):
            self.draw_modes(dt)
        elif self.page == "process":
            self.draw_process(dt)
        elif self.page == "review":
            self.draw_review(dt)
        elif self.page in ("storage", "storage_empty"):
            self.draw_storage(dt)

    def draw_sidebar(self):
        pg = self.pygame
        side = pg.Surface((SIDEBAR_W, self.win_h))
        side.fill((18, 14, 16))
        title = self.font_side.render("Screens", True, self.theme["pink"])
        side.blit(title, (14, 10))
        pg.draw.line(side, self.theme["pinkDim"], (12, 30), (SIDEBAR_W - 12, 30))

        for i, (_id, label) in enumerate(SCREENS):
            y = self._list_top + i * self._row_h
            selected = i == self.screen_i
            if selected:
                pg.draw.rect(side, (36, 20, 30), (8, y, SIDEBAR_W - 16, self._row_h - 2))
                pg.draw.rect(side, self.theme["pink"], (8, y, 3, self._row_h - 2))
            key = str(i + 1) if i < 9 else ""
            prefix = f"{key}  " if key else "   "
            color = self.theme["pink"] if selected else (180, 150, 165)
            surf = self.font_side_sm.render(prefix + label, True, color)
            side.blit(surf, (16, y + 6))

        help_y = self.win_h - 70
        pg.draw.line(side, (40, 28, 34), (12, help_y - 8), (SIDEBAR_W - 12, help_y - 8))
        for j, line in enumerate(
            ("[ ] / Tab  cycle", "← →  modes/storage", "click list item", "Esc  quit")
        ):
            surf = self.font_side_sm.render(line, True, self.theme["muted"])
            side.blit(surf, (14, help_y + j * 14))

        self.screen.blit(side, (0, 0))

    def frame(self, dt: float):
        self.draw_panel_page(dt)
        scaled = self.pygame.transform.scale(self.fb, (W * SCALE, H * SCALE))
        self.screen.fill((12, 10, 12))
        self.draw_sidebar()
        self.screen.blit(scaled, (SIDEBAR_W, 0))
        self.pygame.draw.rect(
            self.screen,
            (50, 40, 48),
            (SIDEBAR_W, 0, W * SCALE, H * SCALE),
            2,
        )
        self.pygame.display.flip()

    def click_sidebar(self, mx: int, my: int):
        if mx >= SIDEBAR_W:
            return
        i = (my - self._list_top) // self._row_h
        if 0 <= i < len(SCREENS):
            self.select_screen(i)

    def handle(self, event):
        if event.type == self.pygame.QUIT:
            self.running = False
        elif event.type == self.pygame.MOUSEBUTTONDOWN and event.button == 1:
            self.click_sidebar(*event.pos)
        elif event.type == self.pygame.KEYDOWN:
            if event.key == self.pygame.K_ESCAPE:
                self.running = False
            elif event.key in (self.pygame.K_TAB, self.pygame.K_RIGHTBRACKET):
                self.select_screen(self.screen_i + 1)
            elif event.key == self.pygame.K_LEFTBRACKET:
                self.select_screen(self.screen_i - 1)
            elif self.pygame.K_1 <= event.key <= self.pygame.K_9:
                idx = event.key - self.pygame.K_1
                if idx < len(SCREENS):
                    self.select_screen(idx)
            elif event.key in (self.pygame.K_LEFT, self.pygame.K_a):
                if self.page in ("storage",):
                    self.nudge_storage(-1)
                else:
                    self.nudge_modes(-1)
            elif event.key in (self.pygame.K_RIGHT, self.pygame.K_d):
                if self.page in ("storage",):
                    self.nudge_storage(1)
                else:
                    self.nudge_modes(1)
            elif event.key == self.pygame.K_r and self.page == "boot":
                self.boot_ms = 0

    def run(self):
        self.select_screen(0)
        while self.running:
            dt = self.clock.tick(30) / 1000.0
            for event in self.pygame.event.get():
                self.handle(event)
            self.frame(dt)
        self.pygame.quit()

    def export(self, out_dir: Path):
        out_dir.mkdir(parents=True, exist_ok=True)
        for i, (sid, _label) in enumerate(SCREENS):
            self.select_screen(i)
            self.review_live = False
            self.boot_live = False
            if sid == "boot":
                self.boot_ms = 1200
            elif sid == "boot_mid":
                self.boot_ms = 4500
            elif sid == "process":
                self.process_ms = 1200
            elif sid == "review":
                self.review_ms = 800
            self.frame(0)
            path = out_dir / f"{sid}.png"
            self.pygame.image.save(self.fb, str(path))
            print(path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export", type=Path, help="Write PNG frames and exit")
    parser.add_argument(
        "--screen",
        choices=[s[0] for s in SCREENS],
        help="Start on a specific screen id",
    )
    args = parser.parse_args()
    theme = load_theme()
    logo = load_logo()
    try:
        preview = Preview(theme, logo)
    except ImportError:
        print(
            "pygame required — enter nix develop (flake includes pygame)",
            file=sys.stderr,
        )
        return 1
    if args.screen:
        ids = [s[0] for s in SCREENS]
        preview.select_screen(ids.index(args.screen))
    if args.export:
        preview.export(args.export)
        preview.pygame.quit()
        return 0
    preview.run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
