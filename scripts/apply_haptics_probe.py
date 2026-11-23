#!/usr/bin/env python3
"""Enable a conservative, honest Music Haptics readiness probe in MediaBloom ONLY.

Run after appearance integration, before Xcode compilation. This is not a
silent beat generator or audio analyzer: Apple requires ISRC Now Playing data
from an actual playback app. Never modify BloomBox primary, guest runtime or JIT.
"""
from pathlib import Path
import plistlib

HOST = Path("LiveContainerSwiftUI/Views/BloomBox/BloomBoxLauncherRoot.swift")
INFO = Path("LiveContainer/Info.plist")
SOURCE = Path("../media-tools/ui/MediaBloomHaptics.swift")
MARK = "// MARK: - MediaBloom Music Haptics system diagnostic"

def replace_once(text, old, new, reason):
    n = text.count(old)
    if n != 1:
        raise RuntimeError(f"{reason}: expected exactly one anchor, got {n}")
    return text.replace(old, new, 1)

def main():
    text = HOST.read_text(encoding="utf-8")
    if MARK in text:
        raise RuntimeError("Music Haptics diagnostic already integrated")
    for requirement in ("struct MBMediaTagEditor: View", "struct MediaBloomAppearanceSheet: View",
                        "MBAlbumArtworkCache"):
        if requirement not in text:
            raise RuntimeError(f"Missing MediaBloom prerequisite: {requirement}")
    text = replace_once(text, "import SwiftUI\\n",
                        "import SwiftUI\\nimport CoreHaptics\\nimport MediaAccessibility\\nimport MediaPlayer\\n",
                        "Music Haptics framework imports")
    haptics = SOURCE.read_text(encoding="utf-8")
    haptics = "\\n".join(line for line in haptics.splitlines()
                        if not line.startswith("import "))
    text += "\\n\\n" + MARK + "\\n" + haptics + "\\n"
    HOST.write_text(text, encoding="utf-8")

    metadata = plistlib.loads(INFO.read_bytes())
    if metadata.get("BloomBoxMediaEdition") is not True:
        raise RuntimeError("Only the independent MediaBloom IPA may opt in")
    if metadata.get("MusicHapticsSupported") not in (None, True):
        raise RuntimeError("Unexpected existing MusicHapticsSupported value")
    metadata["MusicHapticsSupported"] = True
    INFO.write_bytes(plistlib.dumps(metadata, sort_keys=False))
    print("PASS: Music Haptics capability and opt-in diagnostic, no fabricated synchronization")

if __name__ == "__main__":
    main()
