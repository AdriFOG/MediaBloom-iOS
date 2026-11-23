#!/usr/bin/env python3
"""Offline safety and integration checks for favorite artist recommendations.

CI does not make live requests, depend on an API key, or write to the user's
MusicBrainz/ListenBrainz accounts. Manual device tests are still necessary.
"""
from pathlib import Path

host = Path('LiveContainerSwiftUI/Views/BloomBox/BloomBoxLauncherRoot.swift').read_text()
source = Path('../media-tools/ui/MediaBloomMusicDiscovery.swift').read_text()
script = Path('../media-tools/scripts/apply_music_discovery.py').read_text()
assert 'MediaBloomOnboardingCompleteV1' in host
assert 'fullScreenCover(isPresented: $showOnboarding)' in host
assert 'MBMusicArtistPicker(model: model' in host
assert 'MusicBrainz' in host
assert 'MusicBrainzArtistMBID' not in host
assert 'MediaBloomFavoriteArtistsV1' in host
assert 'MediaBloomArtistMBIDsV1' in host
assert 'MediaBloomPreferredMusicAppIDV1' in host
assert 'MBMusicRecommendationCard(' in host
assert 'onOpenSong: { model.onPlaySong($0) }' in host
assert 'model.onPlaySong = { song in' in host
assert 'try await player.runApp(multitask: false, urlStr: link.absoluteString)' in host
assert 'UIApplication.shared.open(link)' not in script
assert 'artistIDs.contains(artist.id)' in host
assert 'return nil' in host
assert 'https://musicbrainz.org/ws/2/artist/' in host
assert 'https://api.listenbrainz.org/1/popularity/top-recordings-for-artist/' in host
assert 'https://coverartarchive.org/release/' in host
assert 'musicbrainz.org", "api.listenbrainz.org' in host
assert 'await MBMusicBrainzLimiter.shared.waitForSlot()' in host
assert 'nextAllowed = reserved.addingTimeInterval(1.25)' in host
assert 'MBMusicServiceError.http(status)' in host
assert 'Retry-After' in host
assert 'searchError = intro + error.localizedDescription + fallback' in host
assert 'MBMusicCatalogAPI.verifiedMatches(name)' in host
assert 'suggestions = MBMusicCatalogAPI.verifiedMatches(name)' in host
assert 'f4abc0b5-3f7a-4eff-8f78-ac078dbce533' in host
assert 'url.queryItems = [URLQueryItem(name: "query", value: sanitized)' in host
assert 'max(0, reserved.timeIntervalSince(now))' in host
assert 'User-Agent' in host
assert 'cacheAge: TimeInterval = 12 * 60 * 60' in host
assert 'MBMusicCatalogAPI.searchURL' in host
assert 'recording_mbid' in host
assert 'inc", value: "url-rels"' in host
assert 'URLSession.shared.data(for: request)' in host
# No third-party secret should ever be copied into iPhone applications.
for forbidden in ('api_key=', 'shared_secret', 'last.fm/api', 'authorization: bearer'):
    assert forbidden not in source.lower(), forbidden
# No hard-coded favorite selection: examples are search suggestions only.
assert 'ForEach(["Bad Bunny", "The Weeknd", "Billie Eilish"]' in source
assert 'model.addArtist(artist.name)' in source
assert 'model.favoriteArtists.sorted()' in source
assert 'model.toggleArtist(name)' in source
assert 'guard !favorites.isEmpty else' in source
assert 'throw URLError' in source
# The featured song must fill its card, not remain a tiny 55pt pill.
assert 'private func fullArtworkCard(' in source
assert 'coverBackground(track, size: size)' in source
assert 'fullArtworkCard(track, size: geometry.size)' in source
assert '.frame(width: size.width, height: size.height)' in source
assert '.frame(maxWidth: 265)' not in source
assert 'front-500' in source
assert 'onOpenSong(track)' in source
assert 'currentIndex = (currentIndex + 1) % songs.count' in source
# Every light panel uses the same translucent, adjustable frosted-glass layer.
assert 'struct MBGlassPanel: View' in host
assert '.fill(.ultraThinMaterial)' in host
assert 'MediaBloomGlassOpacityV1' in host
assert 'glassSection' in host
assert 'MBGlassPanel(tint: glassColor, opacity: glassOpacity' in host
# Preserve the existing guest link dispatch, favorite selection and volume.
assert 'runApp(multitask: false, urlStr: link.absoluteString)' in host
assert 'MediaBloomMasterGainV1' in host
# Mixed catalog: real popularity first, verified MB releases/recordings for artists
# with no ListenBrainz totals (common for smaller Spanish/Latin bands).
assert "private func listenBrainzTracks(_ artist:" in host
assert "private func musicBrainzReleaseTracks(_ artist:" in host
assert "private func musicBrainzRecordingTracks(_ artist:" in host
assert "private func discographyTracks(_ artist:" in host
assert 'https://musicbrainz.org/ws/2/release/' in host
assert 'URLQueryItem(name: "artist", value: artist.id)' in host
assert 'URLQueryItem(name: "status", value: "official")' in host
assert 'URLQueryItem(name: "inc", value: "recordings+artist-credits")' in host
assert 'URLQueryItem(name: "inc", value: "artist-credits")' in host
assert 'credits(recording, include: artist.id) != false' in host
assert 'if result.count < 4' in host
assert 'let fallback = try await discographyTracks(artist)' in host
assert 'if seen.insert(key).inserted { result.append(song) }' in host
assert '"MediaBloomRecommendedTracksV2." + artist.id' in host
assert '"MediaBloomTopTracksV1." + artist.id' not in host
assert 'for position in 0..<12' in host
assert 'for group in groups where position < group.count' in host
assert 'coverReleaseID: releaseID' in host
print('PASS: favorite-only discovery; public sources; honest search fallback; no secrets')