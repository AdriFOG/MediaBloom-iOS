#!/usr/bin/env python3
"""MediaBloom appearance and per-guest/master audio contract after overlay."""
from pathlib import Path
import re

host = Path('LiveContainerSwiftUI/Views/BloomBox/BloomBoxLauncherRoot.swift').read_text()
audio = Path('TweakLoader/TweakLoader.m').read_text()
control = Path('../media-tools/controls/MediaBloomControlCenter.swift').read_text()

assert 'struct MediaBloomAppearanceSheet: View' in host
assert 'MBWallpaperStore.save(image)' in host
assert 'MediaBloomWallpaperBlurV1' in host
assert 'MediaBloomAccentHexV1' in host
assert 'MediaBloomCardHexV1' in host
assert 'MediaBloomMixCardHexV1' in host
assert 'MBPhotosSelection' in host and 'allowedContentTypes: [.image]' in host
assert 'MediaBloomReferenceView(model: model)' in host
assert 'private func portraitLayout' in host and 'private func landscapeLayout' in host
assert 'private func individualVolumeEditor' in host
assert 'private func appVolumeContextMenu' in host
assert '.contextMenu { appVolumeContextMenu(app) }' in host
assert 'MediaBloomMasterIndependentV2' in host
assert 'MediaBloomAudioProbeEnabledV1' in host
assert 'MediaBloomControlCenterSync.notify()' in host
assert 'setMasterVolume(' in host
assert 'MediaBloomAudioGainV1.' in host
assert 'private static let audioGuestIDs: Set<String>' in host
# Guest-specific callbacks and original guest slider never clobber general gain.
bridge = host.split('struct BloomBoxMediaHome: View', 1)[1].split('struct BloomBoxMediaHomeClassic: View', 1)[0]
assert 'shared.set(safeValue, forKey: "MediaBloomMasterGainV1")' not in bridge
assert 'shared.set(safeValue, forKey: "MediaBloomAudioGainV1." + id.lowercased())' in bridge
assert 'MBCurrentGain(void)' in audio
current_gain = audio.split('static float MBCurrentGain(void)', 1)[1].split('static float MBRequestedBaseVolume',1)[0]
assert 'MediaBloomMasterGainV1' in current_gain
assert 'MediaBloomAudioGainV1.' in current_gain
assert 'individual * master' in current_gain
assert 'return powf(combined, 2.6f);' in current_gain
assert 'MBIsMediaHost()' in audio
supported = audio.split('static BOOL MBIsSupportedGuest(', 1)[1].split('static BOOL MBIsMediaHost(',1)[0]
for media in ['com.google.ios.youtube', 'com.google.ios.youtubemusic',
              'com.spotify.client', 'com.netflix.netflix', 'org.videolan.vlc-ios']:
    assert media in supported, media
assert 'com.husk.app' not in supported
assert 'games' not in supported.lower()
# The floating gain only adjusts its own app.
overlay = audio.split('- (void)storeValue:(float)value', 1)[1].split('- (void)sliderMoved:',1)[0]
assert 'setFloat:normalized forKey:key' in overlay
assert 'forKey:@"MediaBloomMasterGainV1"' not in overlay
assert 'defaults.set(slider, forKey: masterKey)' in control
assert 'for guest in guestIDs' not in control
assert 'MPVolumeView' not in control
assert 'AVAudioSession.sharedInstance().outputVolume' not in host
# Material previously inherited the dark app scheme and had an opaque
# 48–96% wash. This regression test keeps real translucent LIGHT glass.
assert 'struct MBGlassPanel: View' in host
assert '.fill(.ultraThinMaterial)' in host
assert r'.environment(\.colorScheme, .light)' in host
assert 'let colorWash = 0.035 + 0.24 * strength' in host
assert 'tint.opacity(max(0.48, min(0.96, opacity)))' not in host
assert 'MediaBloomGlassOpacityV1' in host
assert 'private var glassSection: some View' in host
assert 'MBGlassPanel(tint: theme.white, opacity: glassOpacity' in host
print('PASS: portrait/landscape polish, wallpaper, colors, independent master + guest gain')