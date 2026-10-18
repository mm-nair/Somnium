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
        lighting_finish = "; ".join(context.get("artifacts") or []) or "retain the existing lighting"
        return template.format(
            style_id=style_id,
            style_label=style.get("label", style_id),
            style_keywords=style.get("keywords", ""),
            context_summary=lighting_finish,
            context=lighting_finish,
            taken_at=context.get("taken_at", ""),
            location_label=context.get("location_label", ""),
            prompt=style.get("prompt", ""),
            variation=". ".join((context.get("variation") or {}).values()),
        ).strip()


class EchoPromptProvider(TemplatePromptProvider):
    pass
