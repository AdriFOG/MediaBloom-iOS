#!/usr/bin/env python3
"""Read BloomBox's user-defined Music/Video tags in MediaBloom's host launcher.

This overlay is applied only to MediaBloom's independent checkout. Both apps
read the same authorized LiveContainer App Group; no app data is copied or
migrated. User labels override the limited built-in media catalog.
"""
from pathlib import Path

launcher = Path("LiveContainerSwiftUI/Views/BloomBox/BloomBoxLauncherRoot.swift")
source = launcher.read_text()
before = """        guard app.uiIsShared else { return nil }
        let bundle = app.bundleIdentifier.lowercased()
"""
after = """        guard app.uiIsShared else { return nil }
        // The primary BloomBox writes this key for each installed guest.
        // Explicit user choices always win over name/ID auto-discovery.
        // Never expose private guest apps in a second container instance.
        let bundle = app.bundleIdentifier.lowercased()
        let storedLabel = LCUtils.appGroupUserDefault.string(
            forKey: "BloomBoxMediaTagV1." + bundle)
        switch storedLabel {
        case "music": return .music
        case "video": return .video
        case "none": return nil
        default: break
        }
"""
if source.count(before) != 1:
    raise SystemExit("Expected exactly one BloomBoxMediaCatalog.kind guard; source changed")
launcher.write_text(source.replace(before, after, 1))
print("PASS MediaBloom shared tags: music/video/none overrides; auto fallback; shared-only")
