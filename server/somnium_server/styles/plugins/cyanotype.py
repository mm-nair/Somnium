"""Example style plugin.

Enable it by adding ``somnium_server.styles.plugins.cyanotype`` to
``style_plugins`` in ``server/settings.json``, then restart or
``POST /v1/styles/reload``. It is not loaded by default.
"""

from __future__ import annotations

STYLE = {
    "id": "cyanotype",
    "label": "CYANOTYPE",
    "keywords": "blueprint cyanotype, prussian blue, sun-print paper",
    "real": True,
    "liberal": True,
    "preview": "previews/cyanotype.jpg",
    "tint": [20, 70, 140],
    "negative": "no added lettering, no extra objects, no changed crop",
    "tuning": {
        "strength": 0.72,
        "temperature": 0.35,
        "prompt_model": "grok-3-mini",
    },
    "prompt": (
        "Reprint the supplied photo as a cyanotype sun-print on cotton paper. "
        "Use prussian blue and pale paper white, with exposure falling off gently "
        "toward the existing frame edges. Keep the original subjects, object count, "
        "poses, camera angle, crop, and background arrangement. "
        "The time of day is {day_part} and the weather reads as {weather}. "
        "Lighting finish only: {context}."
    ),
}


def register(registry) -> None:
    registry.add(STYLE, source="plugin:cyanotype")
