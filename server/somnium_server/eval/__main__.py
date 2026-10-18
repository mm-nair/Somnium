"""Score a pair of JPEGs, or cook one image through the mock or live path.

From the repository root:

    PYTHONPATH=server python -m somnium_server.eval pair orig.jpg dream.jpg
    PYTHONPATH=server python -m somnium_server.eval mock photo.jpg --style pencil
    SOMNIUM_EVAL_LIVE=1 PYTHONPATH=server python -m somnium_server.eval live photo.jpg --style pencil

``pair`` and ``mock`` stay on this machine. ``live`` calls the configured
prompt and image providers and refuses to run unless SOMNIUM_EVAL_LIVE is set.
"""

from __future__ import annotations

import argparse
import json
import sys
import tempfile
from pathlib import Path

from somnium_server.eval.harness import (
    LiveEvalDisabled,
    MockImagine,
    evaluate_pair,
    evaluate_pipeline,
    run_live,
)
from somnium_server.eval.metrics import STRICT_THRESHOLDS


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Somnium scene-preservation eval")
    sub = parser.add_subparsers(dest="cmd", required=True)

    pair = sub.add_parser("pair", help="Score two existing images")
    pair.add_argument("original", type=Path)
    pair.add_argument("dream", type=Path)
    pair.add_argument("--strict", action="store_true")

    mock = sub.add_parser("mock", help="Cook with MockImagine (no network)")
    mock.add_argument("image", type=Path)
    mock.add_argument("--style", default="pencil")

    live = sub.add_parser("live", help="Cook with the real providers")
    live.add_argument("image", type=Path)
    live.add_argument("--style", default="pencil")

    args = parser.parse_args(argv)
    try:
        if args.cmd == "pair":
            score = evaluate_pair(
                args.original.read_bytes(),
                args.dream.read_bytes(),
                thresholds=STRICT_THRESHOLDS if args.strict else None,
            )
            report = score.as_dict()
        elif args.cmd == "mock":
            report = _mock_report(args.image, args.style)
        else:
            report = run_live(args.image, args.style)
    except LiveEvalDisabled as err:
        print(str(err), file=sys.stderr)
        return 2
    except (OSError, ValueError) as err:
        print(str(err), file=sys.stderr)
        return 2

    print(json.dumps(report, indent=2))
    passed = report["passed"] if "passed" in report else report["score"]["passed"]
    return 0 if passed else 1


def _mock_report(image: Path, style: str) -> dict:
    from somnium_server.pipeline import Pipeline

    with tempfile.TemporaryDirectory() as tmp:
        cfg = {
            "server": {"output_dir": tmp},
            "styles": {
                style: {
                    "label": style,
                    "keywords": "",
                    "prompt": "Restyle the photo. Lighting finish only: {context}.",
                    "real": True,
                }
            },
            "context": {"provider": "stub"},
            "prompt": {"provider": "template"},
            "image": {"provider": "echo"},
            "location": {},
            "creative": {},
        }
        pipeline = Pipeline(cfg)
        pipeline.stack["image"] = MockImagine()
        return evaluate_pipeline(pipeline, image.read_bytes(), style_id=style)


if __name__ == "__main__":
    raise SystemExit(main())
