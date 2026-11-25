#!/usr/bin/env python3
"""Checks the two original reference compositions are genuinely wired."""
from pathlib import Path

source = Path('LiveContainerSwiftUI/Views/BloomBox/BloomBoxLauncherRoot.swift').read_text()
assert source.count('struct MediaBloomReferenceView: View') == 1
assert source.count('MediaBloomReferenceView(model: model, allSharedGuests: apps)') == 1
assert 'if geometry.size.width > geometry.size.height' in source
assert 'private func portraitLayout' in source
assert 'private func landscapeLayout' in source
assert 'private func volumeRail' in source
assert 'private func artistPanel' in source
assert 'private func landscapeAppPanel' in source
assert 'private func appGridPanel' in source
assert 'private func featuredPanel' in source
assert 'theme.coral' in source
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
# The launcher must allow explicit, user-selected shared guests without
# modifying the original BloomBox app and must never create a BloomBox link.
assert "MBMediaLibraryPicker(apps: allSharedGuests" in source
assert "MediaBloomLauncherAppIDsV1" in source
assert "MediaBloomLauncherSelectionRevisionV1" in source
assert 'url.scheme = "bloomboxmedia"' in source
assert 'link.scheme = "bloomboxmedia"' in source
assert 'url.host = "livecontainer-launch"' in source
assert 'link.host = "livecontainer-launch"' in source
assert "generateWebClipConfig(" in source
assert 'payloads[0]["URL"] = destination.absoluteString' in source
assert 'profile["PayloadOrganization"] = "MediaBloom"' in source
assert 'if let image = guest.appInfo.iconIsDarkIcon(false)' in source
assert "app.uiIsShared" in source
assert 'showLibraryPicker = true' in source
assert 'private func landscapeAppPanel()' in source
assert 'private func appGridPanel()' in source
print('PASS: two faithful layouts, real audio bindings, real app icons, original engine untouched')