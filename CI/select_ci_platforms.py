#!/usr/bin/env python3
"""Select manual CI platforms without creating jobs for unrequested targets."""
import json
import sys
from pathlib import Path

CHOICES = {"all", "linux", "windows", "macos", "android", "ios"}


def select_matrix(platform: str) -> dict:
    if platform not in CHOICES:
        raise ValueError(f"Unknown platform {platform!r}. Expected: {', '.join(sorted(CHOICES))}")
    path = Path(__file__).with_name("build_platform_matrix.json")
    items = json.loads(path.read_text(encoding="utf-8"))
    selected = [
        {key: value for key, value in item.items() if key != "group"}
        for item in items if platform == "all" or item["group"] == platform
    ]
    if not selected:
        raise ValueError(f"Missing matrix entries for {platform}")
    return {"include": selected}


if __name__ == "__main__":
    selection = sys.argv[1] if len(sys.argv) > 1 else "all"
    print("matrix=" + json.dumps(select_matrix(selection), separators=(",", ":")))
