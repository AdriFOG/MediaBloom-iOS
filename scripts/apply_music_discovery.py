#!/usr/bin/env python3
"""Wire public MetaBrainz music discovery into MediaBloom's pinned Swift host.

Order: after apply_reference_ui.py and apply_media_customization.py.
No credentials, privileged APIs, Spotify tokens, game paths or injected guest
hooks are changed by this script.
"""
from pathlib import Path

HOST = Path('LiveContainerSwiftUI/Views/BloomBox/BloomBoxLauncherRoot.swift')
SWIFT = Path('../media-tools/ui/MediaBloomMusicDiscovery.swift')
MARKER = '// MARK: - MediaBloom favorites-only public music discovery'


def replace_once(source: str, before: str, after: str, contract: str) -> str:
    count = source.count(before)
    if count != 1:
        raise RuntimeError(f'{contract}: expected one anchor, found {count}')
    return source.replace(before, after, 1)


def main():
    host = HOST.read_text(encoding='utf-8')
    if MARKER in host:
        raise RuntimeError('Music discovery already injected')
    for contract in ['MediaBloomReferenceView(model: model, allSharedGuests: apps)', 'struct MBReferenceTheme',
                     'MediaBloomMasterIndependentV2',
                     'struct BloomBoxMediaHome: View',
                     'struct BloomBoxMediaHomeClassic: View']:
        if contract not in host:
            raise RuntimeError(f'Missing integration prerequisite: {contract}')

    host = replace_once(host,
        '    var onPlayRecommendation: () -> Void = {}\n',
        '    var onPlayRecommendation: () -> Void = {}\n'
        '    var onPlaySong: (MBMusicTrack) -> Void = { _ in }\n',
        'recommendation playback hook')

    bridge_start = host.index('struct BloomBoxMediaHome: View {')
    bridge_end = host.index('// MARK: - MediaBloom reference-accurate vertical and horizontal layouts', bridge_start)
    assert bridge_end > bridge_start
    prefix, bridge, suffix = host[:bridge_start], host[bridge_start:bridge_end], host[bridge_end:]
    bridge = replace_once(bridge,
        '    @State private var showSettings = false\n',
        '    @State private var showSettings = false\n'
        '    @State private var songLaunchError = ""\n'
        '    @State private var showSongLaunchError = false\n',
        'Media bridge alerts')
    bridge = replace_once(bridge,
        '''            .sheet(isPresented: $showSettings) {
                BloomBoxMediaHomeClassic(apps: apps, launch: launch)
            }
''',
        '''            .sheet(isPresented: $showSettings) {
                BloomBoxMediaHomeClassic(apps: apps, launch: launch)
            }
            .alert("MediaBloom", isPresented: $showSongLaunchError) {
                Button("OK", role: .cancel) {}
            } message: {
                Text(songLaunchError)
            }
''',
        'Song URL launch alert')

    # MediaBloom selects a currently installed, shared player, and reuses
    # LCAppModel.runApp's existing urlStr guest launch path. Do not open URLs
    # with UIApplication.shared.open, which could leave MediaBloom/LiveContainer.
    target = '        model.onOpenSettings = { showSettings = true }\n'
    insert = '''        model.onPlaySong = { song in
            let supported: Set<String> = ["com.google.ios.youtubemusic", "com.spotify.client",
                "com.apple.music", "com.deezer.deezer", "com.google.ios.youtube"]
            let available = mediaApps.filter {
                $0.uiIsShared && supported.contains($0.bundleIdentifier.lowercased())
            }
            let preferred = UserDefaults.standard.string(forKey: "MediaBloomPreferredMusicAppIDV1") ?? ""
            guard let player = available.first(where: { $0.bundleIdentifier == preferred }) ??
                  available.first(where: { $0.bundleIdentifier.lowercased() == "com.google.ios.youtubemusic" }) ??
                  available.first else {
                songLaunchError = "Instala o comparte una app musical compatible con MediaBloom."
                showSongLaunchError = true
                return
            }
            Task { @MainActor in
                // A recording ID is not a Spotify/YouTube track ID. Resolve a
                // real linked recording where available; otherwise be honest
                // and open that song's search in the selected guest.
                let exact = await MBMusicCatalogAPI.shared.exactSongURL(
                    for: song, appID: player.bundleIdentifier)
                guard let link = exact ?? MBMusicCatalogAPI.searchURL(
                    for: song, appID: player.bundleIdentifier) else {
                    songLaunchError = "No existe un enlace compatible para esta canción."
                    showSongLaunchError = true
                    return
                }
                do {
                    try await player.runApp(multitask: false, urlStr: link.absoluteString)
                } catch {
                    songLaunchError = "No se pudo abrir \\(song.title) en \\(player.displayName): \\(error.localizedDescription)"
                    showSongLaunchError = true
                }
            }
        }
'''
    bridge = replace_once(bridge, target, insert + target, 'Wire selected music app')
    host = prefix + bridge + suffix
    contents = SWIFT.read_text(encoding='utf-8')
    # Keep the source in ui/ for review and append into the same Swift file as
    # MediaBloomReferenceView/MediaBloomHomeModel in the ephemeral Xcode checkout.
    contents = '\n'.join(line for line in contents.splitlines()
                         if not line.startswith('import '))
    host += '\n\n' + MARKER + '\n' + contents + '\n'
    HOST.write_text(host, encoding='utf-8')
    print('PASS: favorites-only MB/LB/CAA discovery and LC guest URL routing wired')


if __name__ == '__main__':
    main()