#!/usr/bin/env python3
"""MediaBloom-only wallpaper, independent master and per-guest volume integration.

Apply after apply_reference_ui.py, before Control Center installation. Patches the
pinned engine in a disposable CI checkout. Never alters the parent BloomBox repo.
"""
from pathlib import Path

HOST = Path('LiveContainerSwiftUI/Views/BloomBox/BloomBoxLauncherRoot.swift')
RUNTIME = Path('TweakLoader/TweakLoader.m')
APPEARANCE = Path('../media-tools/ui/MediaBloomAppearance.swift')
MARK = '// MARK: - MediaBloom independent wallpaper, palette and per-app gain'

# Known supported multimedia bundle IDs, not a blanket tweak for game guests.
MEDIA_IDS = [
    'com.google.ios.youtube', 'com.google.ios.youtubemusic',
    'com.spotify.client', 'com.amazon.mp3', 'com.soundcloud.touchapp',
    'com.deezer.deezer', 'com.tidal', 'com.apple.music',
    'com.netflix.netflix', 'com.amazon.aiv', 'com.twitch.twitch',
    'tv.twitch', 'org.videolan.vlc-ios', 'com.plexapp.plex',
    'com.firecore.infuse', 'com.disney.disneyplus',
]


def replace_once(src, before, after, what):
    count = src.count(before)
    if count != 1:
        raise RuntimeError(f'{what}: expected one anchor; got {count}')
    return src.replace(before, after, 1)


def apply():
    text = HOST.read_text(encoding='utf-8')
    if MARK in text or 'struct MBReferenceTheme' in text:
        raise RuntimeError('MediaBloom customization already applied')
    if 'MediaBloomReferenceView(model: model)' not in text:
        raise RuntimeError('Reference music layouts must be installed first')

    ids = '\n'.join('        "' + value + '",' for value in MEDIA_IDS)
    text = replace_once(text,
        '''    private static let audioGuestIDs: Set<String> = [
        "com.google.ios.youtube", "com.google.ios.youtubemusic"
    ]''',
        '    private static let audioGuestIDs: Set<String> = [\n' + ids + '\n    ]',
        'host supported media catalog')

    # The former master key was a *mirror* of whichever per-app slider moved.
    # Keep the name for widget/guest compatibility, migrate exactly once to
    # 100% so existing 2% per-app settings do not get applied twice.
    before = '''    @MainActor private func sync() {
        let palette: [Color] = ['''
    after = '''    @MainActor private func sync() {
        let shared = LCUtils.appGroupUserDefault
        if !shared.bool(forKey: "MediaBloomMasterIndependentV2") {
            shared.set(1.0, forKey: "MediaBloomMasterGainV1")
            shared.set(true, forKey: "MediaBloomMasterIndependentV2")
            shared.synchronize()
        }
        let palette: [Color] = ['''
    text = replace_once(text, before, after, 'master-gain migration')

    # Per-app changes must not overwrite global gain.
    text = replace_once(text,
        '''            shared.set(safeValue, forKey: "MediaBloomAudioGainV1." + id.lowercased())
            shared.set(safeValue, forKey: "MediaBloomMasterGainV1")
''',
        '''            shared.set(safeValue, forKey: "MediaBloomAudioGainV1." + id.lowercased())
''', 'per-guest bridge setter')
    text = replace_once(text,
        '''        shared.set(value, forKey: "MediaBloomMasterGainV1")
        shared.synchronize()
''',
        '''        shared.synchronize()
''', 'classic per-app slider')

    # Original launcher imports UIKit; image picker is iOS 14+, file picker iOS 15+.
    text = replace_once(text, 'import UIKit\n',
                        'import UIKit\nimport PhotosUI\nimport UniformTypeIdentifiers\n',
                        'Photo and file picker imports')
    appearance_source = APPEARANCE.read_text(encoding='utf-8')
    appearance_source = '\n'.join(line for line in appearance_source.splitlines()
                                  if not line.startswith('import '))
    text += '\n\n' + MARK + '\n' + appearance_source + '\n'
    HOST.write_text(text, encoding='utf-8')

    audio = RUNTIME.read_text(encoding='utf-8')
    old_check = '''static BOOL MBIsSupportedGuest(NSString *identifier) {
    NSString *name = identifier.lowercaseString;
    return [name isEqualToString:@"com.google.ios.youtube"] ||
           [name isEqualToString:@"com.google.ios.youtubemusic"];
}'''
    ids_objc = ',\n        '.join('@"' + value + '"' for value in MEDIA_IDS)
    new_check = '''static BOOL MBIsSupportedGuest(NSString *identifier) {
    if (identifier.length == 0) return NO;
    static NSSet<NSString *> *allowed;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        allowed = [NSSet setWithArray:@[
        ''' + ids_objc + '''
        ]];
    });
    return [allowed containsObject:identifier.lowercaseString];
}'''
    audio = replace_once(audio, old_check, new_check, 'guest media-only allowlist')

    old_gain = '''    NSString *key = [@"MediaBloomAudioGainV1." stringByAppendingString:gMBAudioGuest ?: @""];
    id value = [shared objectForKey:key];
    float slider = [value respondsToSelector:@selector(floatValue)]
        ? MBClampVolume([value floatValue]) : 1.0f;
    // Human hearing does not interpret raw PCM/AVPlayer amplitude linearly.
    // A 50% control used to mean 0.50 signal (-6 dB) and felt too weak.
    // The exponent creates a stronger but smooth 0..100% attenuation curve.
    // 100% is unity, and 0% remains true silence; never amplify > 1.0.
    if (slider < 0.0001f) return 0.0f;
    return powf(slider, 2.6f);'''
    new_gain = '''    // Two INDEPENDENT controls. Both are attenuation only and neither can
    // change the actual hardware volume or boost output above 100%.
    NSString *key = [@"MediaBloomAudioGainV1." stringByAppendingString:gMBAudioGuest ?: @""];
    id individualRaw = [shared objectForKey:key];
    id masterRaw = [shared objectForKey:@"MediaBloomMasterGainV1"];
    float individual = [individualRaw respondsToSelector:@selector(floatValue)]
        ? MBClampVolume([individualRaw floatValue]) : 1.0f;
    float master = [masterRaw respondsToSelector:@selector(floatValue)]
        ? MBClampVolume([masterRaw floatValue]) : 1.0f;
    float combined = individual * master;
    if (combined < 0.0001f) return 0.0f;
    return powf(combined, 2.6f);'''
    audio = replace_once(audio, old_gain, new_gain, 'master times individual attenuation')

    # Overlay slider is a guest-only adjustment. The master Widget icon must
    # represent the general level, even if an app's floating slider moves.
    old_overlay = '''    // The Control Center icon must also reflect changes from the floating
    // BloomAudio slider; previously only the App Intent wrote the master key.
    [NSUserDefaults.lcSharedDefaults setFloat:normalized
                                       forKey:@"MediaBloomMasterGainV1"];
'''
    audio = replace_once(audio, old_overlay,
        '''    // Floating slider edits this guest only; general master stays unchanged.
''', 'floating in-guest slider')
    old_label = '''    self.guestLabel.text = [gMBAudioGuest containsString:@"music"] ? @"YouTube Music" : @"YouTube";'''
    new_label = '''    NSDictionary<NSString *, NSString *> *names = @{
        @"com.google.ios.youtube": @"YouTube", @"com.google.ios.youtubemusic": @"YouTube Music",
        @"com.spotify.client": @"Spotify", @"com.netflix.netflix": @"Netflix",
        @"com.amazon.mp3": @"Amazon Music", @"com.soundcloud.touchapp": @"SoundCloud",
        @"com.deezer.deezer": @"Deezer", @"com.tidal": @"Tidal",
        @"com.apple.music": @"Apple Music", @"com.amazon.aiv": @"Prime Video",
        @"com.twitch.twitch": @"Twitch", @"tv.twitch": @"Twitch",
        @"org.videolan.vlc-ios": @"VLC", @"com.plexapp.plex": @"Plex",
        @"com.firecore.infuse": @"Infuse", @"com.disney.disneyplus": @"Disney+"
    };
    self.guestLabel.text = names[gMBAudioGuest] ?: gMBAudioGuest.lastPathComponent;'''
    audio = replace_once(audio, old_label, new_label, 'guest bubble display name')
    RUNTIME.write_text(audio, encoding='utf-8')
    print('PASS: MediaBloom appearance, master x per-guest audio and known media whitelist installed')


if __name__ == '__main__':
    apply()