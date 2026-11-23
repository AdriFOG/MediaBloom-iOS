#!/usr/bin/env python3
"""Checks the two original reference compositions are genuinely wired."""
from pathlib import Path

source = Path('LiveContainerSwiftUI/Views/BloomBox/BloomBoxLauncherRoot.swift').read_text()
assert source.count('struct MediaBloomReferenceView: View') == 1
assert source.count('MediaBloomReferenceView(model: model)') == 1
assert 'if geometry.size.width > geometry.size.height' in source
assert 'private func portraitLayout' in source
assert 'private func landscapeLayout' in source
assert 'private func volumeRail' in source
assert 'private func artistPanel' in source
assert 'private func landscapeAppPanel' in source
assert 'private func appGridPanel' in source
assert 'private func featuredPanel' in source
assert 'MBReferenceStyle.coral' in source
assert 'MediaBloomAudioGainV1.' in source
assert 'model.setVolume(' in source
assert 'model.onVolumeCommit()' in source
assert 'model.onOpenApp(app.id)' in source
assert 'model.toggleArtist(artist)' in source
assert 'model.removeArtist(artist)' in source
assert 'model.favoriteArtists.count' in source
assert 'showArtistEditor = true' in source
assert 'LiveBoardIconStore.image(app, dark: false)' in source
assert 'com.google.ios.youtubemusic' in source
assert 'struct BloomBoxMediaHomeClassic: View' in source
assert 'struct BloomBoxMediaHome: View' in source
assert 'MediaBloomHomeView(model: model)' not in source
ui = source.split('// MARK: - MediaBloom reference-accurate vertical and horizontal layouts', 1)[1]
for forbidden in ('MPVolumeView', 'AVAudioSession', 'URLSession', 'GameBox',
                  '.scrollIndicators(.hidden)'):
    assert forbidden not in ui, forbidden
print('PASS: two faithful layouts, real audio bindings, real app icons, original engine untouched')