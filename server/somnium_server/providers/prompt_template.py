from __future__ import annotations

from typing import Any

from somnium_server.styles.template import render_template, template_variables


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
        variables = template_variables(style_id=style_id, style=style, context=context)
        return render_template(template, variables)


class EchoPromptProvider(TemplatePromptProvider):
    pass
