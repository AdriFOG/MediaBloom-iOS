// MediaBloom controls: genuine Control Center buttons, never an overlaid iOS HUD.
// Compiled ONLY into the MediaBloom WidgetKit app extension (iOS 18+).
import AppIntents
import Foundation
import Security
import SwiftUI
import WidgetKit

enum MediaBloomVolumeStore {
    static let prefix = "MediaBloomAudioGainV1."
    static let musicID = "com.google.ios.youtubemusic"
    static let videoID = "com.google.ios.youtube"

    // Match LiveContainer's App Group selection. SideStore/AltStore may add
    // the signing team's identifier to the group when installed on device.
    static func sharedDefaults() -> UserDefaults? {
        var team = ""
        if let task = SecTaskCreateFromSelf(nil),
           let value = SecTaskCopyValueForEntitlement(
            task, "com.apple.developer.team-identifier" as CFString, nil
           ) as? String {
            team = value
        }
        let base = ["group.com.SideStore.SideStore", "group.com.rileytestut.AltStore"]
        var candidates: [String] = []
        if !team.isEmpty {
            candidates += base.map { $0 + "." + team }
        }
        candidates += base
        let fileManager = FileManager.default
        let available = candidates.compactMap { group -> (String, URL)? in
            guard let location = fileManager.containerURL(
                forSecurityApplicationGroupIdentifier: group
            ) else { return nil }
            return (group, location)
        }
        // The app group that contains LiveContainer's shared Apps directory
        // is the one MediaBloom and BloomBox already use.
        guard let group = (available.first {
            fileManager.fileExists(atPath: $0.1.appendingPathComponent("Apps").path)
        } ?? available.first)?.0 else { return nil }
        return UserDefaults(suiteName: group)
    }

    @discardableResult static func change(source: String, direction: Int) -> Bool {
        guard let defaults = sharedDefaults(),
              defaults.bool(forKey: "MediaBloomAudioProbeEnabledV1") else { return false }
        let target = source == "video" ? videoID : musicID
        let key = prefix + target
        let value = defaults.object(forKey: key) as? NSNumber
        let current = value?.doubleValue ?? 1.0
        let initial = current.isFinite ? max(0, min(1, current)) : 1
        // Direction -1/+1 = 10-point volume changes, 0 toggles mute.
        let next: Double
        switch direction {
        case -1: next = max(0, initial - 0.1)
        case 1: next = min(1, initial + 0.1)
        case 0:
            let backupKey = "MediaBloomControlRestoreV1." + target
            if initial > 0.001 {
                defaults.set(initial, forKey: backupKey)
                next = 0
            } else {
                let stored = defaults.object(forKey: backupKey) as? NSNumber
                next = max(0.1, min(1, stored?.doubleValue ?? 0.5))
            }
        default: return false
        }
        defaults.set(next, forKey: key)
        return true
    }
}

@available(iOSApplicationExtension 18.0, *)
struct MediaBloomChangeVolumeIntent: AppIntent {
    static var title: LocalizedStringResource = "MediaBloom volume"
    static var description = IntentDescription("Change MediaBloom's own per-app audio gain.")

    @Parameter(title: "Source")
    var source: String

    @Parameter(title: "Step")
    var direction: Int

    init() {}

    init(source: String, direction: Int) {
        self.source = source
        self.direction = direction
    }

    func perform() async throws -> some IntentResult {
        _ = MediaBloomVolumeStore.change(source: source, direction: direction)
        return .result()
    }
}

@available(iOSApplicationExtension 18.0, *)
struct MediaBloomMusicUp: ControlWidget {
    var body: some ControlWidgetConfiguration {
        StaticControlConfiguration(kind: "com.kdt.BloomBoxMedia.volume.music.up") {
            ControlWidgetButton(action: MediaBloomChangeVolumeIntent(source: "music", direction: 1)) {
                Label("Music +", systemImage: "speaker.plus")
            }
        }
        .displayName("MediaBloom Música +")
        .description("Subir el volumen independiente de YouTube Music.")
    }
}
@available(iOSApplicationExtension 18.0, *)
struct MediaBloomMusicDown: ControlWidget {
    var body: some ControlWidgetConfiguration {
        StaticControlConfiguration(kind: "com.kdt.BloomBoxMedia.volume.music.down") {
            ControlWidgetButton(action: MediaBloomChangeVolumeIntent(source: "music", direction: -1)) {
                Label("Music −", systemImage: "speaker.minus")
            }
        }
        .displayName("MediaBloom Música −")
        .description("Bajar el volumen independiente de YouTube Music.")
    }
}
@available(iOSApplicationExtension 18.0, *)
struct MediaBloomMusicMute: ControlWidget {
    var body: some ControlWidgetConfiguration {
        StaticControlConfiguration(kind: "com.kdt.BloomBoxMedia.volume.music.mute") {
            ControlWidgetButton(action: MediaBloomChangeVolumeIntent(source: "music", direction: 0)) {
                Label("Music mute", systemImage: "speaker.slash")
            }
        }
        .displayName("MediaBloom Música silencio")
        .description("Silenciar o restaurar YouTube Music.")
    }
}
@available(iOSApplicationExtension 18.0, *)
struct MediaBloomVideoUp: ControlWidget {
    var body: some ControlWidgetConfiguration {
        StaticControlConfiguration(kind: "com.kdt.BloomBoxMedia.volume.video.up") {
            ControlWidgetButton(action: MediaBloomChangeVolumeIntent(source: "video", direction: 1)) {
                Label("Video +", systemImage: "speaker.plus")
            }
        }
        .displayName("MediaBloom Video +")
        .description("Subir el volumen independiente de YouTube.")
    }
}
@available(iOSApplicationExtension 18.0, *)
struct MediaBloomVideoDown: ControlWidget {
    var body: some ControlWidgetConfiguration {
        StaticControlConfiguration(kind: "com.kdt.BloomBoxMedia.volume.video.down") {
            ControlWidgetButton(action: MediaBloomChangeVolumeIntent(source: "video", direction: -1)) {
                Label("Video −", systemImage: "speaker.minus")
            }
        }
        .displayName("MediaBloom Video −")
        .description("Bajar el volumen independiente de YouTube.")
    }
}
@available(iOSApplicationExtension 18.0, *)
struct MediaBloomVideoMute: ControlWidget {
    var body: some ControlWidgetConfiguration {
        StaticControlConfiguration(kind: "com.kdt.BloomBoxMedia.volume.video.mute") {
            ControlWidgetButton(action: MediaBloomChangeVolumeIntent(source: "video", direction: 0)) {
                Label("Video mute", systemImage: "speaker.slash")
            }
        }
        .displayName("MediaBloom Video silencio")
        .description("Silenciar o restaurar YouTube.")
    }
}
