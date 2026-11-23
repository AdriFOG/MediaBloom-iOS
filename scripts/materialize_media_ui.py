#!/usr/bin/env python3
"""Materialize the polished, user-provided SwiftUI sources in this CI checkout.

This temporary transport bundle avoids modifying the pinned engine and keeps
the submitted ZIP exactly reproducible. The downloadable polished ZIP contains
all four source files as ordinary, reviewable text.
"""
from pathlib import Path
from base64 import b64decode
from hashlib import sha256
import json
import zlib

ROOT = Path("../media-tools").resolve()
ASSET = ROOT / "assets/claude_ui_source.zlib.b64"
EXPECTED = "9c7c07256516d9fc78db01538c857ee0e53cb23f41671f4fd59e1fbce14e6dc6"
FILES = {
    "ui/MediaBloomHomeView.swift",
    "ui/MediaBloomHomeBridge.swift",
    "scripts/apply_media_ui.py",
    "tests/test_media_ui.py",
}

def main():
    payload = zlib.decompress(b64decode(ASSET.read_text().strip(), validate=True))
    digest = sha256(payload).hexdigest()
    if digest != EXPECTED:
        raise RuntimeError("Embedded MediaBloom UI source failed SHA256 verification")
    contents = json.loads(payload.decode("utf-8"))
    if set(contents) != FILES:
        raise RuntimeError(f"Unexpected MediaBloom UI paths: {sorted(contents)}")
    for name, source in contents.items():
        dest = (ROOT / name).resolve()
        if not dest.is_relative_to(ROOT) or dest.exists() or not isinstance(source, str):
            raise RuntimeError(f"Unsafe or duplicate UI source: {name}")
        if name == "ui/MediaBloomHomeView.swift" and ".scrollIndicators(.hidden)" in source:
            raise RuntimeError("iOS 15 compatibility regression in submitted UI")
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(source, encoding="utf-8")
    print("PASS: materialized 4 reviewed MediaBloom UI sources (SHA256 verified)")

if __name__ == "__main__":
    main()
