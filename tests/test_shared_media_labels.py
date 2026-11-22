#!/usr/bin/env python3
"""Contract for BloomBox -> MediaBloom shared media labels."""
from pathlib import Path

text = Path("LiveContainerSwiftUI/Views/BloomBox/BloomBoxLauncherRoot.swift").read_text()
start = text.index("    static func kind(for app: LCAppModel) -> Kind? {")
end = text.index("\n    }\n}", start)
body = text[start:end]
assert 'guard app.uiIsShared else { return nil }' in body
assert '"BloomBoxMediaTagV1." + bundle' in body
assert 'case "music": return .music' in body
assert 'case "video": return .video' in body
assert 'case "none": return nil' in body
assert 'default: break' in body
assert 'if musicIDs.contains(bundle)' in body
assert 'if videoIDs.contains(bundle)' in body
assert body.index('case "music": return .music') < body.index('if musicIDs.contains(bundle)')
assert body.index('guard app.uiIsShared') < body.index('"BloomBoxMediaTagV1."')
assert 'MediaBloomAudioProbeEnabledV1' in text
assert 'BloomBoxMediaAudioSlider' in text
print("PASS: shared-only, explicit labels over auto catalog, audio UI retained")
