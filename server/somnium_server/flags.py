"""Form-field parsing for POST /v1/process."""


def form_flag(value: object) -> bool:
    """True only for a real form string. Direct tests pass None, not a field."""
    if not isinstance(value, str):
        return False
    return value.strip().lower() in {"1", "true", "yes", "on"}
