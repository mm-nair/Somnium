"""Pluggable style engine for Somnium cooks."""

from somnium_server.styles.registry import StyleRegistry
from somnium_server.styles.schema import ALLOWED_VARIABLES, Style, StyleError, validate_style
from somnium_server.styles.template import render_template, template_variables

__all__ = [
    "ALLOWED_VARIABLES",
    "Style",
    "StyleError",
    "StyleRegistry",
    "render_template",
    "template_variables",
    "validate_style",
]
