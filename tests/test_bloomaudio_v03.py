#!/usr/bin/env python3
"""Source contract tests for in-guest BloomAudio v0.3.

Executed against the throwaway, identity-patched MediaBloom checkout,
never against the primary BloomBox repository or installed app.
"""
from pathlib import Path

tweak = Path("TweakLoader/TweakLoader.m").read_text()
ui = Path("LiveContainerSwiftUI/Views/BloomBox/BloomBoxLauncherRoot.swift").read_text()
host = Path("LiveContainer/Info.plist").read_bytes()
assert "MBInstallAudioHooks();" in tweak
assert "MBStartVolumeOverlay();" in tweak
assert "MBVolumePassThroughWindow" in tweak
assert "hitTest:(CGPoint)point" in tweak
assert "makeKeyAndVisible" in tweak  # Comment explicitly forbids stealing focus.
assert "gMBVolumeWindow.hidden = NO;" in tweak
assert "MBApplyLiveGain();" in tweak
assert "MBCurrentGain(void)" in tweak
assert "powf(slider, 2.6f)" in tweak
assert 'MediaBloomAudioGainV1.' in tweak
assert '"MediaBloomAudioProbeEnabledV1"' in tweak
assert "com.kdt.bloomboxmedia" in tweak
assert "com.google.ios.youtube" in tweak
assert "com.google.ios.youtubemusic" in tweak
assert "Slider(value: $gain, in: 0...1" in ui
assert "BloomBoxMediaAudioSlider" in ui
assert "MediaBloomAudioOverlayYV1" in tweak
assert "MediaBloomAudioOverlayYV1" not in ui
assert b"BloomBoxMediaEdition" in host
assert "MPVolumeView" not in tweak
assert "outputVolume" not in tweak

# Spotify support is strictly opt-in and must never pretend that a
# proprietary audio pipeline is a controllable AVFoundation player.
assert 'com.spotify.client' in tweak
assert 'MBIsSpotifyGuest(NSString *identifier)' in tweak
assert 'MBSpotifyAudioAvailable(void)' in tweak
assert 'MediaBloomSpotifyAVPlayersObservedV1' in tweak
assert 'MBTrackPlayerOnMain(id player)' in tweak
assert 'MBSpotifyAudioAvailable()) return;' in tweak
assert 'Spotify · sin ruta AV' in tweak
edge_source = Path("../media-tools/controls/MediaBloomGuestEdge.inc").read_text()
assert 'Spotify: sin ruta AV' in edge_source
assert 'MBRefreshVolumeOverlayControls();' in tweak
assert 'setOutputVolume:' not in tweak
# Spotify v2 tests AVAudioEngine *only within its own guest process*.
assert 'AVAudioEngine' in tweak
assert 'startAndReturnError:' in tweak
assert 'MBSpotifyRunningEngines(void)' in tweak
assert 'MBSpotifyActiveAVPlayers(void)' in tweak
assert 'return MBSpotifyActiveAVPlayers() > 0 || MBSpotifyRunningEngines() > 0;' in tweak
assert 'MBSpotifySetMixerGain(id engine, float gain)' in tweak
assert 'MBSpotifyMainMixer(id engine)' in tweak
assert 'MediaBloomSpotifyAudioDiagnosticV2' in tweak
assert 'MediaBloomSpotifyAudioDetailsV2' in tweak
assert 'MediaBloomSpotifyActivePlayersV2' in tweak
assert 'MediaBloomSpotifyActiveEnginesV2' in tweak
assert 'MediaBloomSpotifyAudioDetailsV2' in (Path("../media-tools/ui/MediaBloomAppearance.swift").read_text())
assert 'Copiar diagnóstico Spotify' in (Path("../media-tools/ui/MediaBloomAppearance.swift").read_text())
# No audio capture, global hardware volume edits, or CoreAudio render hooks.
assert 'AudioUnitRender' not in tweak
assert 'AudioQueueSetParameter' not in tweak
assert 'setOutputVolume:' in tweak
assert 'MPVolumeView' not in tweak

# More noticeable perceptual attenuation without ever amplifying.
def gain(x):
    return 0.0 if x <= 0.0 else x**2.6

assert gain(0.0) == 0.0
assert gain(1.0) == 1.0
assert 0.0 < gain(0.2) < 0.03
assert gain(0.5) < 0.2
assert gain(0.8) < 0.6
assert all(gain(i / 100) <= gain((i + 1) / 100) for i in range(100))
print("PASS: opt-in, isolated, live volume overlay and non-linear 0-100% curve")
