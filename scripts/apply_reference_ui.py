#!/usr/bin/env python3
"""Apply both supplied music-screen reference layouts to MediaBloom's disposable host.

Run after apply_media_ui.py. The original Home remains in source for safe
fallback; the existing launcher bridge is switched to the responsive layout.
Nothing here changes the JIT, BloomAudio hooks, or shared guest storage.
"""
from pathlib import Path

ROOT = Path('.')
LAUNCHER = ROOT / 'LiveContainerSwiftUI/Views/BloomBox/BloomBoxLauncherRoot.swift'
SOURCE = Path('../media-tools/ui/MediaBloomReferenceView.swift')
MARK = '// MARK: - MediaBloom reference-accurate vertical and horizontal layouts'
BEFORE = '        MediaBloomHomeView(model: model)'
AFTER = '        MediaBloomReferenceView(model: model, allSharedGuests: apps)'


def main():
    text = LAUNCHER.read_text(encoding='utf-8')
    if MARK in text:
        raise RuntimeError('Reference visual layout already applied')
    if text.count(BEFORE) != 1:
        raise RuntimeError('Expected exactly one MediaBloomHomeView launcher route')
    for token in ['struct BloomBoxMediaHomeClassic: View',
                  'struct BloomBoxMediaHome: View',
                  'MediaBloomAudioGainV1.', 'onPlayRecommendation']:
        if token not in text:
            raise RuntimeError(f'Missing MediaBloom host contract: {token}')

    # The reference's music button must always open the same app shown in its
    # label. YouTube Music wins when installed; otherwise keep the music fallback.
    old = '''if let app = mediaApps.first(where: { BloomBoxMediaCatalog.kind(for: $0) == .music }) {
                launch(app)
            }'''
    new = '''if let app = mediaApps.first(where: {
                $0.bundleIdentifier.lowercased() == "com.google.ios.youtubemusic"
            }) ?? mediaApps.first(where: { BloomBoxMediaCatalog.kind(for: $0) == .music }) {
                launch(app)
            }'''
    if text.count(old) != 1:
        raise RuntimeError('Music launch bridge changed; check label/target consistency')
    text = text.replace(old, new, 1)

    # A known non-media Controller guest was appearing only due to a user
    # media-tag override. Never show that single non-media launcher by default.
    old = '''apps.filter { BloomBoxMediaCatalog.kind(for: $0) != nil }
            .sorted {'''
    new = '''apps.filter {
            BloomBoxMediaCatalog.kind(for: $0) != nil &&
            $0.displayName.caseInsensitiveCompare("Apple Controller") != .orderedSame
        }
            .sorted {'''
    if text.count(old) != 1:
        raise RuntimeError('Media-only filter changed; do not silently rewrite')
    text = text.replace(old, new, 1)

    text = text.replace(BEFORE, AFTER, 1)
    ref = SOURCE.read_text(encoding='utf-8')
    ref = '\n'.join(line for line in ref.splitlines() if not line.startswith('import '))
    text += '\n\n' + MARK + '\n' + ref + '\n'
    LAUNCHER.write_text(text, encoding='utf-8')
    print('PASS: portrait/landscape reference layouts now replace MediaBloom view; JIT/audio unchanged')


if __name__ == '__main__':
    main()