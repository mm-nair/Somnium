from __future__ import annotations

from typing import Any


class TemplatePromptProvider:
    def synthesize(
        self,
        *,
        style_id: str,
        style: dict[str, Any],
        context: dict[str, Any],
        prompt_cfg: dict[str, Any],
    ) -> str:
        template = style.get("prompt") or prompt_cfg.get("user_template") or (
            "{style_label}: {style_keywords}. Context: {context_summary}"
        )
        return template.format(
            style_id=style_id,
            style_label=style.get("label", style_id),
            style_keywords=style.get("keywords", ""),
            context_summary=context.get("summary", ""),
            context=context.get("summary", ""),
            taken_at=context.get("taken_at", ""),
            location_label=context.get("location_label", ""),
            prompt=style.get("prompt", ""),
        ).strip()


class EchoPromptProvider(TemplatePromptProvider):
    pass
