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
assert "startCapture(english:" in appearance and "stopCapture(english:" in appearance
assert "recordedEvents.suffix(8)" in appearance
assert "UIPasteboard.general.string" in appearance
assert "clearCapture()" in appearance
assert "Timer(timeInterval: 2.0" in source
assert "RunLoop.main.add(timer, forMode: .common)" in source
assert "UserDefaults.standard.set(recordedEvents" in source
assert "UserDefaults.standard.synchronize()" in source
assert "Apple Music Haptics ACTIVE" in source
guest_probe = Path("../media-tools/haptics/MediaBloomGuestNowPlayingProbe.inc").read_text()
guest_official = Path("../media-tools/haptics/MediaBloomGuestOfficialHaptics.inc").read_text()
assert "MBInstallGuestHapticsProbe();" in ui or "MBInstallGuestHapticsProbe();" in Path("TweakLoader/TweakLoader.m").read_text()
loader = Path("TweakLoader/TweakLoader.m").read_text()
assert 'com.google.ios.youtubemusic' in guest_probe
assert 'MBIsMediaHost()' in guest_probe
assert 'MediaBloomHapticsGuestCaptureV1' in guest_probe
assert 'MediaBloomHapticsGuestEventsV1' in guest_probe
assert 'MediaBloomHapticsGuestSamplesV1' in guest_probe
assert 'MPNowPlayingInfoCenter' in guest_probe
assert 'MBInstallGuestHapticsProbe();' in loader
assert "MBMHInspectCurrentSong(info, title, artist, isrc);" in guest_probe
assert "MBMHStartGuestObserver();" in guest_probe
assert "MBMHCheckISRCs(" in guest_official
assert "MBMHSearchRecording(" in guest_official
assert "MBMHApplyVerifiedISRC(" in guest_official
assert "checkHapticTrackAvailabilityForMediaMatchingCode:completionHandler:" in guest_official
assert "checkHapticTrackAvailabilityForMedia:completionHandler:" not in guest_official
assert "MBMHRequestJSONAttempt" in guest_official
assert "MusicBrainz request failed:" in guest_official
assert "retry %lu/2" in guest_official
assert "MBMHLookupSharedISRCs" in guest_official
assert "MBMHRecordingMatchesAlbum" in guest_official
assert "different recordings have no common ISRC" in guest_official
assert "no unsafe ISRC guess" in guest_official
assert "second - nearest >= 2.0" in guest_official
assert 'MBMHSearchRecording(title, artist, MBMHDuration(info), album, signature)' in guest_official
assert "setNowPlayingInfo:" in guest_official
assert "musicbrainz.org/ws/2/recording/" in guest_official
assert "gMBMHNextRequestAt = next + 1.3" in guest_official
assert "MBMHNormalized" in guest_official
assert "MBMHStillCurrent" in guest_official
assert "MediaBloomHapticsAutoISRCV1" in guest_official
assert "MediaBloomHapticsAutoISRCV1" in appearance
assert "automaticMusicHaptics = false" in appearance
assert "Toggle(isOn: $automaticMusicHaptics)" in appearance
assert "!@available" not in guest_official
assert 'LCUtils.appGroupUserDefault.set(false, forKey: guestEnabledKey)' in source
assert 'shared.set(true, forKey: guestEnabledKey)' in source
assert 'haptics.guestSamples' in appearance
assert 'haptics.guestEvents.suffix(12)' in appearance
for forbidden in ('setNowPlayingInfo:', 'AVAudioEngine', 'AVAudioRecorder', 'requestRecordPermission'):
    assert forbidden not in guest_probe
assert "MPNowPlayingInfoCenter.default().nowPlayingInfo" in source
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
