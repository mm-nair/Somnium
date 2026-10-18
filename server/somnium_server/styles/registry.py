"""Style registry.

Load order, later entries replacing the same id:

1. ``creative.json`` modes (built-in, lenient)
2. JSON files in the styles directory, sorted by name
3. ``user_styles`` in settings.json
4. Python plugins listed in ``style_plugins``

A bad user file is skipped and recorded on ``problems``. A bad built-in mode
still raises, because the camera modes are the product.
"""

from __future__ import annotations

import importlib
import json
from pathlib import Path
from typing import Any

from somnium_server.styles.schema import Style, StyleError, validate_style


class StyleRegistry:
    def __init__(
        self,
        *,
        creative: dict[str, Any] | None = None,
        styles_dir: Path | None = None,
        user_styles: list[Any] | None = None,
        plugins: list[str] | None = None,
    ):
        self.creative = creative or {}
        self.styles_dir = styles_dir
        self.user_styles = list(user_styles or [])
        self.plugins = list(plugins or [])
        self._styles: dict[str, Style] = {}
        self._order: list[str] = []
        self.problems: list[str] = []
        self.reload()

    def reload(self) -> None:
        self._styles = {}
        self._order = []
        self.problems = []
        for mode in self.creative.get("modes") or []:
            style = validate_style(mode, source="creative.json", strict=False)
            self._insert(style)
        if self.styles_dir is not None:
            self._load_dir(self.styles_dir)
        for index, raw in enumerate(self.user_styles):
            self._add_user(raw, source=f"settings[{index}]")
        for name in self.plugins:
            self._load_plugin(name)

    def add(self, raw: dict[str, Any], *, source: str, strict: bool = True) -> Style:
        style = validate_style(raw, source=source, strict=strict)
        self._insert(style)
        return style

    def get(self, style_id: str) -> Style | None:
        return self._styles.get(style_id)

    def __contains__(self, style_id: object) -> bool:
        return isinstance(style_id, str) and style_id in self._styles

    def __iter__(self):
        return iter(self._order)

    def all_styles(self, *, enabled_only: bool = False) -> list[Style]:
        styles = [self._styles[style_id] for style_id in self._order]
        if enabled_only:
            return [style for style in styles if style.enabled]
        return styles

    def as_map(self) -> dict[str, dict[str, Any]]:
        """Pipeline view: enabled styles only, creative.json field shape."""
        return {
            style.id: style.as_pipeline_dict()
            for style in self.all_styles(enabled_only=True)
        }

    def _insert(self, style: Style) -> None:
        if style.id not in self._styles:
            self._order.append(style.id)
        self._styles[style.id] = style

    def _load_dir(self, path: Path) -> None:
        if not path.is_dir():
            return
        for file in sorted(path.glob("*.json")):
            if file.name.startswith("."):
                continue
            try:
                raw = json.loads(file.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError) as err:
                self.problems.append(f"{file.name}: {err}")
                continue
            self._add_user(raw, source=f"file:{file.name}")

    def _add_user(self, raw: Any, *, source: str) -> None:
        try:
            self.add(raw, source=source, strict=True)
        except StyleError as err:
            self.problems.append(str(err))

    def _load_plugin(self, name: str) -> None:
        module_name = (name or "").strip()
        if not module_name:
            self.problems.append("empty style plugin name")
            return
        try:
            module = importlib.import_module(module_name)
        except Exception as err:  # noqa: BLE001 — one bad plugin must not stop boot
            self.problems.append(f"{module_name}: {err}")
            return
        try:
            if hasattr(module, "register"):
                module.register(self)
            elif hasattr(module, "STYLE"):
                self.add(module.STYLE, source=f"plugin:{module_name}")
            else:
                self.problems.append(f"{module_name}: no register() or STYLE")
        except StyleError as err:
            self.problems.append(f"{module_name}: {err}")
        except Exception as err:  # noqa: BLE001
            self.problems.append(f"{module_name}: {err}")
