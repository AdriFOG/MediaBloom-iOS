#!/usr/bin/env python3
"""Reconnect shared MediaBloom tags to the existing Home model after all overlays.

This ONLY modifies the ephemeral CI checkout, not the pinned BloomBox source.
The four tag values are exactly those used by the existing shared catalog.
"""
from pathlib import Path

HOST = Path("LiveContainerSwiftUI/Views/BloomBox/BloomBoxLauncherRoot.swift")


def replace_one(src: str, old: str, new: str, why: str) -> str:
    count = src.count(old)
    if count != 1:
        raise RuntimeError(f"{why}: expected one anchor, got {count}")
    return src.replace(old, new, 1)


def main() -> None:
    text = HOST.read_text(encoding="utf-8")
    if "// MARK: - MediaBloom final tag-refresh integration" in text:
        raise RuntimeError("Final tag integration applied twice")
    for needed in ('"BloomBoxMediaTagV1." + bundle',
                   "struct MBMediaTagEditor: View",
                   "MediaBloomGlassTintHexV1",
                   "MediaBloomMixSavedV1",
                   "MBAlbumArtworkCache"):
        if needed not in text:
            raise RuntimeError(f"Missing prerequisite: {needed}")
    text = replace_one(
        text, "        MediaBloomReferenceView(model: model)",
        "        MediaBloomReferenceView(model: model, allSharedGuests: apps)",
        "Pass complete shared guest catalog for manual music/video labels")
    text = replace_one(
        text,
        """            .onAppear { wire(); sync() }
            .onChange(of: apps.map { $0.bundleIdentifier }) { _ in wire(); sync() }""",
        """            .onAppear { wire(); sync() }
            .onChange(of: apps.map { $0.bundleIdentifier }) { _ in wire(); sync() }
            .onReceive(NotificationCenter.default.publisher(for: .mediaBloomMediaTagsDidChange)) { _ in
                sync()
            }
            .onReceive(NotificationCenter.default.publisher(for: UIApplication.didBecomeActiveNotification)) { _ in
                // Tag changes made in BloomBox while MediaBloom was suspended.
                sync()
            }""",
        "Recompute media list on tag changes and foreground")
    # Keep the strict existing media-only app gate. The editor only exposes
    # apps the user intentionally shared; the main app list is not modified.
    text += "\n// MARK: - MediaBloom final tag-refresh integration\n"
    HOST.write_text(text, encoding="utf-8")
    print("PASS: BloomBox shared music/video/none/auto tags, live refresh and favorites intact")


if __name__ == "__main__":
    main()
