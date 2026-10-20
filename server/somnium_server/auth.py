"""Shared-secret check for POST /v1/process.

The laptop bind is open when SOMNIUM_PROCESS_TOKEN is unset. A public host
sets the token and, on the VM, SOMNIUM_REQUIRE_AUTH=1 so a missing secret
refuses to boot instead of accepting anonymous cooks.
"""

from __future__ import annotations

import hashlib
import hmac
import os

from fastapi.responses import JSONResponse
from starlette.requests import Request

_TOKEN_ENV = "SOMNIUM_PROCESS_TOKEN"
_REQUIRE_ENV = "SOMNIUM_REQUIRE_AUTH"
_MAX_TOKEN_LEN = 256
_TRUE = {"1", "true", "yes", "on"}


def process_token() -> str:
    return os.environ.get(_TOKEN_ENV, "").strip()


def auth_required_by_env() -> bool:
    return os.environ.get(_REQUIRE_ENV, "").strip().lower() in _TRUE


def ensure_auth_config() -> None:
    """Exit before bind when a public deploy forgot the shared secret."""
    if auth_required_by_env() and not process_token():
        raise SystemExit(
            "SOMNIUM_PROCESS_TOKEN is required when SOMNIUM_REQUIRE_AUTH=1"
        )


def tokens_match(provided: str, expected: str) -> bool:
    if not provided or not expected:
        return False
    if len(provided) > _MAX_TOKEN_LEN or len(expected) > _MAX_TOKEN_LEN:
        return False
    return hmac.compare_digest(
        hashlib.sha256(provided.encode("utf-8")).digest(),
        hashlib.sha256(expected.encode("utf-8")).digest(),
    )


def presented_token(headers) -> str:
    direct = headers.get("x-somnium-token", "").strip()
    if direct:
        return direct
    auth = headers.get("authorization", "").strip()
    prefix = "bearer "
    if auth.lower().startswith(prefix):
        return auth[len(prefix) :].strip()
    return ""


def rejection(request: Request) -> JSONResponse | None:
    expected = process_token()
    if not expected:
        return None
    if tokens_match(presented_token(request.headers), expected):
        return None
    return JSONResponse(
        {"error": "unauthorized"},
        status_code=401,
        headers={"WWW-Authenticate": "Bearer"},
    )
