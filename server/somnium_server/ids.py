"""Shared id check for devices, boards, and optional COOK tokens."""

from __future__ import annotations

import re

# Letters, digits, dot, underscore, hyphen. No spaces, so a COOK line stays one token.
ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")


def valid_id(value: str | None) -> bool:
    return isinstance(value, str) and ID_RE.fullmatch(value) is not None
