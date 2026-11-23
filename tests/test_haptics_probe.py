#!/usr/bin/env python3
"""Native Music Haptics proof of concept: validate honest readiness only."""
from pathlib import Path
import plistlib
ui = Path("LiveContainerSwiftUI/Views/BloomBox/BloomBoxLauncherRoot.swift").read_text()
info = plistlib.loads(Path("LiveContainer/Info.plist").read_bytes())
source = Path("../media-tools/ui/MediaBloomHaptics.swift").read_text()
appearance = Path("../media-tools/ui/MediaBloomAppearance.swift").read_text()
assert info.get("BloomBoxMediaEdition") is True
assert info.get("MusicHapticsSupported") is True
assert "import CoreHaptics" in ui and "import MediaAccessibility" in ui
assert "MAMusicHapticsManager.shared" in ui
assert "addStatusObserver" in ui
assert "playbackSummary" in ui
assert "MPMediaItemPropertyTitle" in ui
assert "MPMediaItemPropertyArtist" in ui
assert "MBHapticsDiagnostic.shared" in appearance
assert "haptics.beginObservation(english:" in appearance
assert "haptics.nowPlayingSummary" in appearance
assert "haptics.playbackSummary" in appearance
assert "MPNowPlayingInfoPropertyInternationalStandardRecordingCode" in ui
assert "checkHapticTrackAvailabilityForMedia(matchingCode:" in ui
assert "CHHapticEngine.capabilitiesForHardware().supportsHaptics" in ui
assert "CHHapticEngine()" in ui
assert "MBHapticsDiagnostic" in ui and "MBHapticsDiagnostic" in appearance
assert "three short physical test taps" in ui.lower() or "Three short physical test taps" in ui
assert "Button" in appearance and "testHardware(english:" in appearance
assert ".onAppear" in appearance
assert "MediaBloomAudioGainV1." in ui and "MediaBloomMasterGainV1" in ui
assert "try await player.runApp(multitask: false, urlStr: link.absoluteString)" in ui
# No simulated synchronization, false publication of song metadata, or hidden microphone use.
for forbidden in ["AVAudioEngine", "AVAudioRecorder", "AVCaptureSession", "setNowPlayingInfo",
                  "requestRecordPermission"]:
    assert forbidden not in source, forbidden
print("PASS: honest native Music Haptics readiness, tactile test and existing media isolation")
