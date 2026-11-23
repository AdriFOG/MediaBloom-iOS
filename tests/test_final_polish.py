#!/usr/bin/env python3
"""Final MediaBloom regression contract: true glass, shared labels, offline caches.

Offline source checks must run in CI on the fully overlaid MediaBloom host.
Actual guest link playback, album art and iPhone glass still need device tests.
"""
from pathlib import Path

host = Path("LiveContainerSwiftUI/Views/BloomBox/BloomBoxLauncherRoot.swift").read_text()
music = Path("../media-tools/ui/MediaBloomMusicDiscovery.swift").read_text()
appearance = Path("../media-tools/ui/MediaBloomAppearance.swift").read_text()
reference = Path("../media-tools/ui/MediaBloomReferenceView.swift").read_text()
assert "MediaBloomReferenceView(model: model, allSharedGuests: apps)" in host
assert "MBMediaTagEditor(apps: allSharedGuests, english: english)" in host
assert "MediaBloomAppearanceSheet(" in reference
assert "onManageMediaLabels:" in reference
assert "mediaBloomMediaTagsDidChange" in host
assert "UIApplication.didBecomeActiveNotification" in host
assert '"BloomBoxMediaTagV1." + bundle' in host
assert '"BloomBoxMediaTagV1." + app.bundleIdentifier.lowercased()' in host
for entry in ['"auto"', '"music"', '"video"', '"none"']:
    assert entry in appearance, entry
assert "app.uiIsShared" in appearance
assert "shared.synchronize()" in appearance
assert "NotificationCenter.default.post(name: .mediaBloomMediaTagsDidChange" in host
assert "model.apps = mediaApps.enumerated().map" in host

# Distinct palette tint; 0% genuinely means no blur/color/stroke.
assert "MediaBloomGlassTintHexV1" in appearance and "MediaBloomGlassTintHexV1" in reference
assert 'Slider(value: $glassOpacity, in: 0...1, step: 0.01)' in appearance
assert "if amount > 0.001" in appearance
assert "shape.fill(.ultraThinMaterial)" in appearance
assert "shape.fill(tint.opacity(amount * 0.22))" in appearance
assert 'let amount = max(0, min(1, opacity))' in appearance
assert "glassTintHex" in reference and "panelInk" in reference
assert "MediaBloomGlassMigratedV2" in reference

# Saved mixes and images must survive a cold start with bounded storage.
assert "MBAlbumArtworkCache" in music
assert "applicationSupportDirectory" in music
assert 'appendingPathComponent("MediaBloom/ArtworkCache"' in music
assert "50_000_000" in music and "100" in music
assert "memory.totalCostLimit" not in music or "15_000_000" in music
assert "func savedMix(for names:" in music
assert "func saveMix(_ songs:" in music
assert '"MediaBloomMixSavedV1"' in music
assert "if cached.fresh { return }" in music
assert "await MBAlbumArtworkCache.shared.bytes(for:" in music
assert "AsyncImage(url: artworkURL)" not in music

# Fresh randomized order on first load, but no repeat of prior launch's first
# track when multiple recordings exist. Keep existing exact-link guest route.
assert "tracks.shuffled()" in music
assert '"MediaBloomLastInitialRecordingV1"' in music
assert "order.swapAt(0, Int.random(in: 1..<order.count))" in music
assert "songs = order" in music
assert 'try await player.runApp(multitask: false, urlStr: link.absoluteString)' in host
assert 'MediaBloomMasterGainV1' in host
for secret in ("api_key=", "shared_secret", "authorization: bearer"):
    assert secret not in music.lower(), secret
print("PASS: labels synced, 0%-glass tint, capped persistent artwork, mix cache and shuffle")
