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
guest_deezer = Path("../media-tools/haptics/MediaBloomGuestDeezerFallback.inc").read_text()
guest_overlay = Path("../media-tools/haptics/MediaBloomGuestHapticsOverlay.inc").read_text()
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
assert "MBMHFastRefreshNowPlaying();" in guest_probe
assert "MBMHUpdateGuestOverlay();" in guest_probe
assert "static void MBMHForceResync(void);" in guest_probe
assert "static void MBMHUpdateGuestOverlay(void);" in guest_probe
assert "MBMHForceResync();" in guest_overlay
assert "static void MBMHUpdateGuestOverlay(void)" in guest_overlay
assert "UIActivityIndicatorView" in guest_overlay
assert "MBMHOverlayText" in guest_overlay
assert "kMBMHStatusKey" in guest_overlay
assert "MediaBloomHapticsGuestOverlayV1" in guest_overlay
assert "Preparando Music Haptics" in guest_overlay
assert "Resincronizar Music Haptics" in guest_overlay
assert "static void MBMHForceResync(void)" in guest_official
assert "Resync requested for ISRC" in guest_official
assert "playback position preserved" in guest_official
assert "gMBMHLastAutoResyncAt >= 20.0" in guest_official
assert 'MBMHSetStatus(@"resyncing"' in guest_official
assert "MediaBloomHapticsGuestOverlayV1" in appearance
assert "Toggle(isOn: $showHapticsInPlayer)" in appearance
assert "guest_overlay" in Path("../media-tools/scripts/apply_haptics_probe.py").read_text()
for forbidden in ("AVAudioRecorder", "AVAudioEngine", "AVCaptureSession",
                  "AVAudioPlayer", "pauseCommand", "seekCommand", "setElapsedPlaybackTime:",
                  "MPRemoteCommandCenter"):
    assert forbidden not in guest_overlay, forbidden
assert "scheduledTimerWithTimeInterval:0.5" in guest_probe
assert "Fast Music Haptics sync enabled (0.5s checks)" in guest_probe
assert 'MBMHEnabled()' in guest_official
assert "static void MBMHFastRefreshNowPlaying(void)" in guest_official
assert "if (now - gMBMHLastWriteAt < 1.0) return;" in guest_official
assert "if (now - gMBMHLastWriteAt < 12.0) return;" not in guest_official
assert "gMBMHLastInsertionLogAt" in guest_official
assert "now - gMBMHLastInsertionLogAt >= 20.0" in guest_official
assert "MBMHInspectCurrentSong(info, title, artist, isrc);" in guest_official
assert "MBMHCheckISRCs(" in guest_official
assert "MBMHSearchRecording(" in guest_official
assert "MBMHApplyVerifiedISRC(" in guest_official
assert "checkHapticTrackAvailabilityForMediaMatchingCode:completionHandler:" in guest_official
assert "checkHapticTrackAvailabilityForMedia:completionHandler:" not in guest_official
assert "MBMHRequestJSONAttempt" in guest_official
assert "MusicBrainz request failed:" in guest_official
assert "retry %lu/2" in guest_official
assert "MBMHLookupSharedISRCs" in guest_official
assert "MBMHDeezerFallback" in guest_official
assert "MusicBrainz HTTP 503; using Deezer" in guest_official
assert "gMBMHMusicBrainzCooldownUntil" in guest_official
assert "MBMHNormalized(album)" in guest_official
assert "llround(MBMHDuration(info))" not in guest_official
assert "guest_deezer" in Path("../media-tools/scripts/apply_haptics_probe.py").read_text()
assert "https://api.deezer.com/search" in guest_deezer
assert "https://api.deezer.com/track/" in guest_deezer
assert "MBMHDZTrackMatches" in guest_deezer
assert "MBMHDZCollaboratorsMatch" in guest_deezer
assert "MBMHDZCheckCandidates" in guest_deezer
# Version-aware matching: only exact titles (including version suffixes) and
# album-mismatched copies with closely matching duration + shared ISRC.
assert "MBMHDZTrackMatchesMode" in guest_deezer
assert 'fabs([duration doubleValue] - gMBMHCurrentDuration) > 2.0' in guest_deezer
assert "alternateRelease && candidates.count < 2" in guest_deezer
assert "same-version alternate-release" in guest_deezer
assert "MBMHDZCheckCandidates(candidates, 0, nil, signature, alternateRelease)" in guest_deezer
assert '[NSURLQueryItem queryItemWithName:@"limit" value:@"50"]' in guest_deezer
assert "title_short" not in guest_deezer  # Never drop radio/live/remix qualifiers
# Apple negative responses are cached by exact ISRC, never by song title,
# and expire after twelve hours so catalog additions can be retried.
assert "MediaBloomHapticsRejectedISRCV1" in guest_official
assert "MBMHIsRejectedISRC" in guest_official
assert "MBMHRememberRejectedISRC(code)" in guest_official
assert "12.0 * 3600.0" in guest_official
assert "MBMHCheckISRCs(codes, index + 1, signature)" in guest_official
# Expose only the last recorded state after the guest has been closed; don't
# claim that playback is still running in the launcher.
assert "MediaBloomHapticsStatusV1" in guest_official
assert 'MBMHSetStatus(@"playing"' not in guest_official or "playing ? @\"playing\"" in guest_official
assert 'MBMHSetStatus(@"searching"' in guest_official
assert 'MBMHSetStatus(@"checking"' in guest_official
assert 'MBMHSetStatus(@"unavailable"' in guest_official
assert "MediaBloomHapticsStatusV1" in source
assert "guestHapticsLabel(english:" in appearance
assert "haptics.guestHapticsSymbol" in appearance
assert "Último resultado de Music Haptics" in appearance
assert "haptics.retryRejectedISRCs()" in appearance
assert "haptics.guestRejectedCount" in appearance
assert "MBMHCheckISRCs(@[commonCode], 0, signature)" in guest_deezer
assert "candidates.count > 5" in guest_deezer
assert "fabs(" in guest_deezer
assert "different ISRCs; nothing inserted" in guest_deezer
for forbidden in ("AVAudioEngine", "AVAudioRecorder", "AVCaptureSession", "microphone",
                  "setNowPlayingInfo:", "isrc = @", "lyrics", "preview"):
    assert forbidden not in guest_deezer, forbidden
assert "MBMHRecordingMatchesAlbum" in guest_official
assert "MusicBrainz: recordings disagree on ISRC; checking Deezer" in guest_official
assert 'Too many ambiguous MusicBrainz recordings' in guest_official
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

# Temporary Music Haptics progress coexists with Edge; it never remains
# blocking the media player after completion or a manual swipe.
assert 'id edgeChoice = [shared objectForKey:@"MediaBloomEdgeEnabledV1"]' not in guest_overlay
assert "gMBMHOverlayDismissedSong" in guest_overlay
assert "MBMHHideGuestOverlay(YES)" in guest_overlay
assert "UISwipeGestureRecognizerDirectionLeft" in guest_overlay
assert "UISwipeGestureRecognizerDirectionRight" in guest_overlay
assert "UISwipeGestureRecognizerDirectionUp" in guest_overlay
assert '[@[@"searching", @"checking", @"resyncing"] containsObject:state]' in guest_overlay

# The Edge app selector is media-only, user-ordered, limited to four, and
# preserves legacy automatic shortcuts until the user picks a selection.
edge = Path("../media-tools/controls/MediaBloomGuestEdge.inc").read_text()
assert "MediaBloomEdgeAppIDsV1" in edge and "self.selectionSnapshot = selection" in edge
assert "ordered.count == 4" in edge
assert "MBEdgeAppPicker(english: english)" in appearance
assert "LCSharedUtils.appGroupPath()" in appearance
assert "selected.count < 4" in appearance and "selected.move(fromOffsets:" in appearance
assert "BloomBoxMediaTagV1." in appearance
# Spotify v1 reads native Now Playing metadata only, without injecting
# unverified ISRCs, forcing playback or claiming Music Haptics has started.
assert 'MBIsSpotifyGuest(guest)' in guest_probe
assert 'spotify ? 0.75 : 2.0' in guest_probe
assert 'if (!MBIsSpotifyGuest(NSUserDefaults.lcGuestAppId))' in guest_probe
assert 'if (!spotify) MBMHStartGuestObserver();' in guest_probe
assert 'if (!spotify) MBMHUpdateGuestOverlay();' in guest_probe
assert 'if (!spotify && [shared boolForKey:@"MediaBloomHapticsAutoISRCV1"])' in guest_probe
assert 'Spotify Now Playing probe loaded' in guest_probe
assert 'Registrar YouTube Music o Spotify' in appearance
assert 'MediaBloomSpotifyAVPlayersObservedV1' in appearance

print("PASS: honest native Music Haptics readiness, tactile test and existing media isolation")
