#!/usr/bin/env python3
"""Somnium LCD preview — 320x240 panel simulator for the ThinkPad.

  nix develop
  python tools/ui_preview.py
  python tools/ui_preview.py --export /tmp/somnium-ui
  python tools/ui_preview.py --screen storage

Sidebar (or 1–9 / [ ] / Tab) picks a screen.
  Looks / Gallery / Menu: left and right. Esc quits.
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
LOGO_PNG = ROOT / "creative" / "logo.png"
LOGO_H = ROOT / "lib" / "SomniumApp" / "LogoBitmap.h"

W, H = 320, 240
SCALE = 3
SIDEBAR_W = 200
REVIEW_MS = 2200

SCREENS = [
    ("boot", "Boot"),
    ("looks", "Looks"),
    ("looks_surprise", "Looks · Surprise"),
    ("menu", "Menu"),
    ("process_capture", "Cook · capture"),
    ("process_style", "Cook · styling"),
    ("process_receive", "Cook · receiving"),
    ("process_timeout", "Cook · timeout"),
    ("review_dream", "Review · dream"),
    ("review_raw", "Review · raw"),
    ("gallery", "Gallery"),
    ("gallery_empty", "Gallery · empty"),
]

# Plates for the gallery mock. Labels come from creative.json, not the old tokens.
FAKE_PHOTOS = [
    {"label": "COMIC #3", "side": "DREAM", "tint": (220, 80, 180), "mode": "popcomic"},
    {"label": "WATERCOLOR #4", "side": "DREAM", "tint": (255, 160, 60), "mode": "watercolor"},
    {"label": "ANIME #5", "side": "DREAM", "tint": (255, 120, 200), "mode": "anime"},
    {"label": "PENCIL SKETCH #6", "side": "RAW", "tint": (210, 170, 100), "mode": "pencil"},
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
    # Prefer creative/logo.png (source of truth); fall back to generated header.
    if LOGO_PNG.is_file():
        try:
            from PIL import Image

            im = Image.open(LOGO_PNG).convert("RGBA")
            im.thumbnail((160, 56), Image.Resampling.LANCZOS)
            rows = []
            for y in range(im.height):
                row = []
                for x in range(im.width):
                    r, g, b, a = im.getpixel((x, y))
                    row.append((0, 0, 0) if a < 16 else (r, g, b))
                rows.append(row)
            return rows
        except Exception:
            pass
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
    return "pick a look..."


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
        self.previews = self._load_previews()
        self.menu_i = 0

    def _load_previews(self):
        images = {}
        try:
            from PIL import Image, ImageOps
        except ImportError:
            return images
        for mode in self.theme["modes"]:
            name = mode.get("preview")
            if not name:
                continue
            path = ROOT / "creative" / name
            if not path.is_file():
                continue
            with Image.open(path) as image:
                image = ImageOps.exif_transpose(image).convert("RGB")
                image = ImageOps.pad(image, (168, 100), method=Image.Resampling.LANCZOS, color=(0, 0, 0))
                images[mode["id"]] = image
        return images

    def _blit_preview(self, mode_id, box, contain=False):
        image = self.previews.get(mode_id)
        if image is None:
            return False
        x, y, w, h = box
        if contain and image.width and image.height:
            scale = min(w / image.width, h / image.height)
            dw = max(1, int(image.width * scale))
            dh = max(1, int(image.height * scale))
            scaled = image.resize((dw, dh))
            origin = (x + (w - dw) // 2, y + (h - dh) // 2)
        else:
            scaled = image.resize((max(1, w), max(1, h)))
            origin = (x, y)
        self.fb.blit(self.pygame.image.fromstring(scaled.tobytes(), scaled.size, "RGB"), origin)
        return True

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
            "USB*" if wifi else "USB.",
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
        self.wifi_on = True
        self.review_live = False
        self.review_ms = 0
        self.boot_ms = 1200 if self.page == "boot" else self.boot_ms
        if self.page == "looks":
            self.index = 0
        elif self.page == "looks_surprise":
            self.index = 3
        elif self.page == "menu":
            self.menu_i = 0
            self.index = 1
        elif self.page.startswith("process"):
            self.process_ms = 1400
            self.index = 1
        elif self.page == "gallery":
            self.photos = list(FAKE_PHOTOS)
            self.photo_i = len(self.photos) - 1
        elif self.page == "gallery_empty":
            self.photos = []
            self.photo_i = 0
        elif self.page.startswith("review"):
            self.index = 1

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

    def nudge_modes(self, delta: int):
        if delta == 0 or self.page not in ("looks", "looks_surprise"):
            return
        self.index = (self.index + (1 if delta > 0 else -1)) % len(self.theme["modes"])

    def nudge_storage(self, delta: int):
        n = len(self.photos)
        if n <= 0 or delta == 0 or self.page != "gallery":
            return
        self.photo_i = (self.photo_i + (1 if delta > 0 else -1)) % n

    def nudge_menu(self, delta: int):
        if delta == 0 or self.page != "menu":
            return
        self.menu_i = (self.menu_i + (1 if delta > 0 else -1)) % 3

    def draw_looks(self, _dt):
        modes = self.theme["modes"]
        mode = modes[self.index % len(modes)]
        self.fill(self.theme["bg"])
        self.draw_header("Looks", wifi=self.wifi_on)
        self.rect((16, 30, 288, 118), self.theme["panel"])
        self.rect((16, 30, 288, 118), self.theme["pink"], 1)
        img = (76, 36, 168, 100)
        tint = tuple(mode.get("tint") or (80, 40, 60))
        if not self._blit_preview(mode["id"], img):
            self.rect(img, tint)
            self.text(mode["label"], (W // 2, 78), (0, 0, 0), self.font_sm, center=True)
        self.text(mode["label"], (W // 2, 154), self.theme["pink"], self.font, center=True)
        thumb_w, thumb_h, gap = 44, 26, 8
        total = len(modes) * thumb_w + (len(modes) - 1) * gap
        x = (W - total) // 2
        for i, item in enumerate(modes):
            selected = i == self.index % len(modes)
            self.rect((x, 182, thumb_w, thumb_h), self.theme["pink"] if selected else self.theme["pinkDim"], 1)
            box = (x + 2, 184, thumb_w - 4, thumb_h - 4)
            if not self._blit_preview(item["id"], box):
                self.rect(box, tuple(item.get("tint") or (80, 40, 60)))
            x += thumb_w + gap
        self.text(
            "turn: look   click: menu   shutter: shoot",
            (W // 2, 216),
            self.theme["muted"],
            center=True,
        )

    def draw_menu(self, _dt):
        mode = self.theme["modes"][self.index % len(self.theme["modes"])]
        self.fill(self.theme["bg"])
        self.draw_header("Menu")
        self.text("4 plates", (W // 2, 36), self.theme["muted"], center=True)
        rows = ["Gallery", f"Shoot  {mode['label']}", "Back"]
        for i, row in enumerate(rows):
            y = 64 + i * 40
            on = i == self.menu_i
            if on:
                self.rect((24, y, 272, 32), self.theme["panel"])
                self.rect((24, y, 4, 32), self.theme["pink"])
                self.rect((24, y, 272, 32), self.theme["pink"], 1)
            self.text(row, (40, y + 8), self.theme["pink"] if on else self.theme["muted"])
        self.text(
            "turn: move   click: choose   shutter: shoot",
            (W // 2, 214),
            self.theme["muted"],
            center=True,
        )

    def draw_process(self, dt):
        self.process_ms += int(dt * 1000)
        stage = {
            "process_capture": 0,
            "process_style": 2,
            "process_receive": 3,
            "process_timeout": 2,
        }.get(self.page, 2)
        failed = self.page == "process_timeout"
        mode = self.theme["modes"][self.index % len(self.theme["modes"])]
        self.fill(self.theme["bg"])
        title = "Kitchen trouble" if failed else "Cooking"
        self.text(title, (W // 2, 8), self.theme["pink"], self.font, center=True)
        self.text(mode["label"], (W // 2, 28), self.theme["muted"], center=True)
        names = ["CAPTURE", "SEND", "STYLE", "RECEIVE"]
        box_w, gap, box_h = 70, 6, 28
        total = 4 * box_w + 3 * gap
        x = (W - total) // 2
        y = 48
        danger = (220, 80, 80)
        for i, name in enumerate(names):
            current = i == stage
            done = i < stage and not failed
            if failed and current:
                ink = danger
                frame = danger
            elif current or done:
                ink = self.theme["pink"]
                frame = self.theme["pink"]
            else:
                ink = self.theme["muted"]
                frame = self.theme["pinkDim"]
            self.rect((x, y, box_w, box_h), self.theme["panel"])
            self.rect((x, y, box_w, box_h), frame, 1)
            self.text(name, (x + box_w // 2, y + 8), ink, self.font_sm, center=True)
            x += box_w + gap
        status = {
            0: "capturing the frame",
            1: "sending over USB",
            2: "timed out - no reply" if failed else "styling on the laptop",
            3: "receiving the dream",
        }[stage]
        self.text(status, (W // 2, 86), self.theme["muted"], center=True)
        self.text("0:12", (W // 2, 104), self.theme["muted"], center=True)
        icon_y = 128
        for ix in (36, 140, 244):
            self.rect((ix, icon_y, 40, 40), self.theme["panel"])
        self.draw_fire(56, icon_y + 14, self.process_ms)
        self.draw_salt(160, icon_y + 12, self.process_ms)
        self.draw_whisk(264, icon_y + 10, self.process_ms)
        hint = "click dial to return" if failed else "dial locked while cooking"
        self.text(hint, (W // 2, 214), self.theme["muted"], center=True)

    def draw_badge(self, label):
        surf = self.font_sm.render(label, True, self.theme["pink"])
        w = surf.get_width() + 8
        x = W - w - 6
        self.rect((x, 4, w, 14), self.theme["bg"])
        self.fb.blit(surf, (x + 4, 5))

    def draw_preview_chrome(self, kind: str, label: str, index1: int, total: int, side: str):
        self.rect((0, 0, W, 22), self.theme["surface"])
        if kind == "review":
            hint = "turn: flip   click: looks"
        elif total <= 0:
            hint = "click: back"
        else:
            hint = "click: flip   hold: back"
        self.text(hint, (6, 5), self.theme["pink"])
        if kind == "review" or total > 0:
            self.draw_badge(side)
        self.rect((0, H - 28, W, 28), self.theme["surface"])
        if kind == "gallery":
            meta = "no plates yet" if total <= 0 else f"{label}  {index1}/{total}"
        else:
            meta = label
        self.text(meta, (8, H - 18), self.theme["pink"])
        when = time.strftime("%b %d  %H:%M")
        surf = self.font_sm.render(when, True, self.theme["pink"])
        tx = W - surf.get_width() - 8
        self.fb.blit(surf, (tx, H - 18))
        self.pygame.draw.line(
            self.fb, self.theme["pinkDim"], (tx, H - 8), (tx + surf.get_width(), H - 8)
        )

    def draw_review(self, _dt):
        mode = self.theme["modes"][self.index % len(self.theme["modes"])]
        side = "RAW" if self.page == "review_raw" else "DREAM"
        self.fill(self.theme["bg"])
        if side == "DREAM" and self.previews.get(mode["id"]) is not None:
            self._blit_preview(mode["id"], (0, 0, W, H), contain=True)
        else:
            tint = tuple(mode.get("tint") or (60, 30, 50))
            self.fake_photo({"label": mode["label"], "tint": tint})
        self.draw_preview_chrome("review", mode["label"], 0, 0, side)

    def draw_storage(self, _dt):
        n = len(self.photos)
        if n <= 0:
            self.fill(self.theme["bg"])
            self.text("no plates yet", (W // 2, 110), self.theme["muted"], center=True)
            self.draw_preview_chrome("gallery", "", 0, 0, "DREAM")
            return
        photo = self.photos[self.photo_i]
        self.fill(self.theme["bg"])
        if photo["side"] == "DREAM" and self._blit_preview(photo["mode"], (0, 0, W, H), contain=True):
            pass
        else:
            self.fake_photo(photo)
        self.draw_preview_chrome("gallery", photo["label"], self.photo_i + 1, n, photo["side"])

    def draw_panel_page(self, dt: float):
        if self.page == "boot":
            self.draw_boot(dt)
        elif self.page in ("looks", "looks_surprise"):
            self.draw_looks(dt)
        elif self.page == "menu":
            self.draw_menu(dt)
        elif self.page.startswith("process"):
            self.draw_process(dt)
        elif self.page.startswith("review"):
            self.draw_review(dt)
        elif self.page in ("gallery", "gallery_empty"):
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
                self.nudge_storage(-1)
                self.nudge_menu(-1)
                self.nudge_modes(-1)
            elif event.key in (self.pygame.K_RIGHT, self.pygame.K_d):
                self.nudge_storage(1)
                self.nudge_menu(1)
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
                self.boot_live = False
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
