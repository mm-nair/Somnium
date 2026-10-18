from __future__ import annotations

import json
import urllib.error
import urllib.request
from typing import Any

from somnium_server.config import env_secret
from somnium_server.providers.prompt_template import TemplatePromptProvider


class OpenAICompatPromptProvider:
    """Any OpenAI-compatible /v1/chat/completions endpoint."""

    def synthesize(
        self,
        *,
        style_id: str,
        style: dict[str, Any],
        context: dict[str, Any],
        prompt_cfg: dict[str, Any],
    ) -> str:
        key = env_secret(prompt_cfg.get("api_key_env", ""))
        if not key:
            return TemplatePromptProvider().synthesize(
                style_id=style_id, style=style, context=context, prompt_cfg=prompt_cfg
            )

        base = prompt_cfg.get("base_url", "https://api.openai.com/v1").rstrip("/")
        user = TemplatePromptProvider().synthesize(
            style_id=style_id, style=style, context=context, prompt_cfg=prompt_cfg
        )
        liberal = bool(style.get("liberal"))
        strength = (
            "LIBERAL stylization allowed for this style: fully commit to the selected "
            "medium and its treatment of existing faces and materials. "
            "Still same crop — no new background scene, no readable text."
            if liberal
            else "Keep the source rendering and apply the specified finish."
        )
        temp = float(prompt_cfg.get("temperature", 0.45))
        if liberal and not prompt_cfg.get("temperature_locked"):
            temp = min(0.75, temp + 0.15)
        body = {
            "model": prompt_cfg.get("model", "gpt-4o-mini"),
            "temperature": temp,
            "messages": [
                {"role": "system", "content": prompt_cfg.get("system", "")},
                {
                    "role": "user",
                    "content": (
                        f"Style id: {style_id}\n"
                        f"Strength: {strength}\n"
                        f"Draft to refine:\n{user}"
                    ),
                },
            ],
        }
        if "top_p" in prompt_cfg:
            body["top_p"] = float(prompt_cfg["top_p"])
        req = urllib.request.Request(
            f"{base}/chat/completions",
            data=json.dumps(body).encode(),
            headers={
                "Authorization": f"Bearer {key}",
                "Content-Type": "application/json",
            },
            method="POST",
        )
        try:
            with urllib.request.urlopen(req, timeout=90) as resp:
                payload = json.loads(resp.read().decode())
            text = payload["choices"][0]["message"]["content"].strip()
            return text or user
        except urllib.error.HTTPError as err:
            # Rate limit / capacity → use the filter draft so cook still runs.
            if err.code in (429, 503):
                return user
            detail = err.read().decode(errors="replace")[:400]
            raise RuntimeError(f"prompt HTTP {err.code}: {detail}") from err
        except (urllib.error.URLError, KeyError, IndexError, json.JSONDecodeError) as err:
            raise RuntimeError(f"prompt provider failed: {err}") from err
