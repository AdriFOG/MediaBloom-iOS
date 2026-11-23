// MediaBloom: user-installed Control Center actions for independent guest audio.
// Only built by MediaBloom's isolated IPA workflow, never primary BloomBox.
// iOS owns the actual volume buttons and system volume slider. These six
// user-added controls change per-guest BloomAudio gain in the shared App Group.
import AppIntents
import Foundation
import SwiftUI
import WidgetKit

@available(iOSApplicationExtension 18.0, *)
private enum MBControlAudio {
    static let music = "com.google.ios.youtubemusic"
    static let video = "com.google.ios.youtube"
    static let prefix = "MediaBloomAudioGainV1."
    static let savedNonZeroPrefix = "MediaBloomControlLastNonZeroV1."
    static let gainStep: Double = 0.10

    // iOS widget extensions have no Swift SecTask API for reading entitlements.
    // Probe only the two App Groups declared in THIS widget's entitlements;
    // containerURL returns nil unless the extension is authorized to use them.
    // Never pretend a regular UserDefaults store is a cross-app suite.
    static func sharedDefaults() -> UserDefaults? {
        let allowed = [
            "group.com.SideStore.SideStore",
            "group.com.rileytestut.AltStore"
        ]
        let available = allowed.compactMap { group -> (String, URL)? in
            guard let url = FileManager.default
                .containerURL(forSecurityApplicationGroupIdentifier: group) else {
                return nil
            }
            return (group, url)
        }
        let preferred = available.first { entry in
            FileManager.default.fileExists(atPath: entry.1.appendingPathComponent("Apps").path)
        } ?? available.first
        guard let selected = preferred else { return nil }
        return UserDefaults(suiteName: selected.0)
    }

    // Five visual states for a small cup filling with water.
    // Actual gain keeps 10% adjustments; icon snaps to the nearest quarter.
    static func glassLevel(for bundle: String) -> Int {
        guard let defaults = sharedDefaults() else { return 100 }
        let saved = defaults.object(forKey: prefix + bundle) as? NSNumber
        let raw = saved?.doubleValue ?? 1.0
        let clamped = raw.isFinite ? min(1, max(0, raw)) : 1.0
        return Int((clamped * 4).rounded()) * 25
    }

    @MainActor static func refreshVisualControls() {
        // iOS re-renders each control with currentValue() from shared defaults.
        // Reload all six so +, − and mute show the same updated water level.
        ControlCenter.shared.reloadAllControls()
    }

    static func modify(_ bundle: String, by change: Double = 0, mute: Bool = false) {
        guard let defaults = sharedDefaults() else {
            NSLog("[MediaBloom Controls] No authorized shared App Group for Control Center")
            return
        }
        let key = prefix + bundle
        let savedKey = savedNonZeroPrefix + bundle
        let old = defaults.object(forKey: key) as? NSNumber
        let value = min(1.0, max(0.0, old?.doubleValue ?? 1.0))
        let new: Double
        if mute {
            if value > 0.001 {
                defaults.set(value, forKey: savedKey)
                new = 0
            } else {
                let previous = defaults.object(forKey: savedKey) as? NSNumber
                new = min(1, max(0.05, previous?.doubleValue ?? 0.5))
            }
        } else {
            new = min(1, max(0, (value * 10 + change * 10).rounded() / 10))
        }
        defaults.set(new, forKey: key)
        defaults.synchronize()
        NSLog("[MediaBloom Controls] %@ volume set to %.2f", bundle, new)
    }
}

// The current volume is read whenever iOS renders or refreshes a control.
// No polling, persistent background task, hardware-button interception, or
// changes to the system volume are needed.
@available(iOSApplicationExtension 18.0, *)
private struct MBGlassLevelProvider: ControlValueProvider {
    let bundle: String

    var previewValue: Int { 50 }

    func currentValue() async throws -> Int {
        MBControlAudio.glassLevel(for: bundle)
    }
}

// Control Center executes these App Intents even when MediaBloom's interface
// is not open. Whether the audio changes immediately depends on the guest
// player still running; suspended players apply the saved gain on resumption.
@available(iOSApplicationExtension 18.0, *)
struct MBMusicLouderIntent: AppIntent {
    static let title: LocalizedStringResource = "MediaBloom: Música +"
    func perform() async throws -> some IntentResult {
        MBControlAudio.modify(MBControlAudio.music, by: MBControlAudio.gainStep)
        await MBControlAudio.refreshVisualControls()
        return .result()
    }
}
@available(iOSApplicationExtension 18.0, *)
struct MBMusicQuieterIntent: AppIntent {
    static let title: LocalizedStringResource = "MediaBloom: Música −"
    func perform() async throws -> some IntentResult {
        MBControlAudio.modify(MBControlAudio.music, by: -MBControlAudio.gainStep)
        await MBControlAudio.refreshVisualControls()
        return .result()
    }
}
@available(iOSApplicationExtension 18.0, *)
struct MBMusicMuteIntent: AppIntent {
    static let title: LocalizedStringResource = "MediaBloom: Silenciar música"
    func perform() async throws -> some IntentResult {
        MBControlAudio.modify(MBControlAudio.music, mute: true)
        await MBControlAudio.refreshVisualControls()
        return .result()
    }
}
@available(iOSApplicationExtension 18.0, *)
struct MBVideoLouderIntent: AppIntent {
    static let title: LocalizedStringResource = "MediaBloom: Video +"
    func perform() async throws -> some IntentResult {
        MBControlAudio.modify(MBControlAudio.video, by: MBControlAudio.gainStep)
        await MBControlAudio.refreshVisualControls()
        return .result()
    }
}
@available(iOSApplicationExtension 18.0, *)
struct MBVideoQuieterIntent: AppIntent {
    static let title: LocalizedStringResource = "MediaBloom: Video −"
    func perform() async throws -> some IntentResult {
        MBControlAudio.modify(MBControlAudio.video, by: -MBControlAudio.gainStep)
        await MBControlAudio.refreshVisualControls()
        return .result()
    }
}
@available(iOSApplicationExtension 18.0, *)
struct MBVideoMuteIntent: AppIntent {
    static let title: LocalizedStringResource = "MediaBloom: Silenciar video"
    func perform() async throws -> some IntentResult {
        MBControlAudio.modify(MBControlAudio.video, mute: true)
        await MBControlAudio.refreshVisualControls()
        return .result()
    }
}

@available(iOSApplicationExtension 18.0, *)
struct MBMusicLouderControl: ControlWidget {
    var body: some ControlWidgetConfiguration {
        StaticControlConfiguration(
            kind: "com.kdt.bloomboxmedia.music.up",
            provider: MBGlassLevelProvider(bundle: MBControlAudio.music)
        ) { level in
            ControlWidgetButton(action: MBMusicLouderIntent()) {
                Label("Música + \(level)%", image: "MediaBloomGlass\(level)Plus")
            }
        }
        .displayName("MediaBloom · Música +")
        .description("Sube 10% el volumen de YouTube Music, sin cambiar el iPhone.")
    }
}
@available(iOSApplicationExtension 18.0, *)
struct MBMusicQuieterControl: ControlWidget {
    var body: some ControlWidgetConfiguration {
        StaticControlConfiguration(
            kind: "com.kdt.bloomboxmedia.music.down",
            provider: MBGlassLevelProvider(bundle: MBControlAudio.music)
        ) { level in
            ControlWidgetButton(action: MBMusicQuieterIntent()) {
                Label("Música − \(level)%", image: "MediaBloomGlass\(level)Minus")
            }
        }
        .displayName("MediaBloom · Música −")
        .description("Baja 10% el volumen de YouTube Music, sin cambiar el iPhone.")
    }
}
@available(iOSApplicationExtension 18.0, *)
struct MBMusicMuteControl: ControlWidget {
    var body: some ControlWidgetConfiguration {
        StaticControlConfiguration(
            kind: "com.kdt.bloomboxmedia.music.mute",
            provider: MBGlassLevelProvider(bundle: MBControlAudio.music)
        ) { level in
            ControlWidgetButton(action: MBMusicMuteIntent()) {
                Label("Silenciar música \(level)%", image: "MediaBloomGlass\(level)Mute")
            }
        }
        .displayName("MediaBloom · Silenciar música")
        .description("Silencia o restaura YouTube Music.")
    }
}
@available(iOSApplicationExtension 18.0, *)
struct MBVideoLouderControl: ControlWidget {
    var body: some ControlWidgetConfiguration {
        StaticControlConfiguration(
            kind: "com.kdt.bloomboxmedia.video.up",
            provider: MBGlassLevelProvider(bundle: MBControlAudio.video)
        ) { level in
            ControlWidgetButton(action: MBVideoLouderIntent()) {
                Label("Video + \(level)%", image: "MediaBloomGlass\(level)Plus")
            }
        }
        .displayName("MediaBloom · Video +")
        .description("Sube 10% el volumen de YouTube.")
    }
}
@available(iOSApplicationExtension 18.0, *)
struct MBVideoQuieterControl: ControlWidget {
    var body: some ControlWidgetConfiguration {
        StaticControlConfiguration(
            kind: "com.kdt.bloomboxmedia.video.down",
            provider: MBGlassLevelProvider(bundle: MBControlAudio.video)
        ) { level in
            ControlWidgetButton(action: MBVideoQuieterIntent()) {
                Label("Video − \(level)%", image: "MediaBloomGlass\(level)Minus")
            }
        }
        .displayName("MediaBloom · Video −")
        .description("Baja 10% el volumen de YouTube.")
    }
}
@available(iOSApplicationExtension 18.0, *)
struct MBVideoMuteControl: ControlWidget {
    var body: some ControlWidgetConfiguration {
        StaticControlConfiguration(
            kind: "com.kdt.bloomboxmedia.video.mute",
            provider: MBGlassLevelProvider(bundle: MBControlAudio.video)
        ) { level in
            ControlWidgetButton(action: MBVideoMuteIntent()) {
                Label("Silenciar video \(level)%", image: "MediaBloomGlass\(level)Mute")
            }
        }
        .displayName("MediaBloom · Silenciar video")
        .description("Silencia o restaura YouTube.")
    }
}
