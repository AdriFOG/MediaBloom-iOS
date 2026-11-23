// MediaBloom: ONE native, system-wide Control Center volume control.
// This is a ControlWidget extension, not an overlay on another app.
// Tap cycles through 100 → 75 → 50 → 25 → 0 → 100 percent.
//
// Use Apple's supported SF Symbols (not raster asset images) so the icon
// displays on iOS 18–27. It changes in five visible volume states.
// Audio changes are per-guest, NEVER the global iPhone output volume.
import AppIntents
import Foundation
import Security
import SwiftUI
import WidgetKit

@available(iOSApplicationExtension 18.0, *)
private enum MBMasterVolume {
    static let kind = "com.kdt.bloomboxmedia.master.volume.v2"
    static let masterKey = "MediaBloomMasterGainV1"
    static let guestIDs = [
        "com.google.ios.youtubemusic",
        "com.google.ios.youtube"
    ]
    static let groupBases = [
        "group.com.SideStore.SideStore",
        "group.com.rileytestut.AltStore"
    ]

    // SideStore may append the signing Team ID to each group.
    // Discover that Team ID using the PUBLIC keychain API, the same fallback
    // used by LCSharedUtils.teamIdentifier in LiveContainer. SecTask* is NOT
    // exposed to Swift app extensions and must not be used here.
    static func signingTeamID() -> String? {
        let account = "mediabloom-widget-team-probe"
        let service = "com.kdt.BloomBoxMedia.ControlCenter"
        let lookup: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrAccount as String: account,
            kSecAttrService as String: service
        ]
        var attributes: CFTypeRef?
        var find = lookup
        find[kSecReturnAttributes as String] = kCFBooleanTrue
        var status = SecItemCopyMatching(find as CFDictionary, &attributes)
        if status == errSecItemNotFound {
            var add = lookup
            add[kSecValueData as String] = Data([1])
            add[kSecReturnAttributes as String] = kCFBooleanTrue
            status = SecItemAdd(add as CFDictionary, &attributes)
        }
        guard status == errSecSuccess,
              let result = attributes as? [String: Any],
              let accessGroup = result[kSecAttrAccessGroup as String] as? String,
              let component = accessGroup.split(separator: ".").first,
              !component.isEmpty else { return nil }
        return String(component)
    }

    static func sharedDefaults() throws -> UserDefaults {
        let team = signingTeamID()
        let suffixed = team.map { id in groupBases.map { $0 + "." + id } } ?? []
        // Some SideStore/LiveContainer resigner builds append the team
        // suffix twice to nested extension groups. Probe only if authorized.
        let twiceSuffixed = team.map { id in groupBases.map { $0 + "." + id + "." + id } } ?? []
        let names = suffixed + twiceSuffixed + groupBases
        let found: [(String, URL)] = names.compactMap { name in
            guard let url = FileManager.default
                .containerURL(forSecurityApplicationGroupIdentifier: name) else {
                return nil
            }
            return (name, url)
        }
        // Do not accept an arbitrary App Group merely because the widget
        // can open it. It might belong to another store/app, so the Intent
        // would report success while MediaBloom's real volume never changes.
        // LiveContainer puts installed guests under the shared "Apps" folder.
        let selected = found.first { entry in
            FileManager.default.fileExists(
                atPath: entry.1.appendingPathComponent("Apps").path)
        } ?? found.first { entry in
            // A valid MediaBloom suite can also be identified by the flag
            // the host persisted when the user enabled BloomAudio.
            UserDefaults(suiteName: entry.0)?
                .object(forKey: "MediaBloomAudioProbeEnabledV1") != nil
        }
        guard let chosen = selected,
              let defaults = UserDefaults(suiteName: chosen.0) else {
            NSLog("[MediaBloom] Control Center cannot open an authorized shared App Group. Team: %@", team ?? "unknown")
            throw MBControlError.groupUnavailable
        }
        return defaults
    }

    static func currentLevel(defaults: UserDefaults) -> Int {
        defaults.synchronize()
        let current = defaults.object(forKey: masterKey)
            ?? defaults.object(forKey: "MediaBloomAudioGainV1." + guestIDs[0])
            ?? defaults.object(forKey: "MediaBloomAudioGainV1." + guestIDs[1])
        let raw = (current as? NSNumber)?.doubleValue ?? 1.0
        let safe = raw.isFinite ? max(0, min(1, raw)) : 1.0
        return Int((safe * 4.0).rounded()) * 25
    }

    // A ControlValueProvider must always produce a renderable value.
    // Throwing while WidgetKit prepares the icon left empty circles on iOS.
    // -1 means "App Group unavailable" rather than pretending volume is 100%.
    // The App Intent still throws, so failures cannot be reported as success.
    static func level() -> Int {
        do {
            return currentLevel(defaults: try sharedDefaults())
        } catch {
            NSLog("[MediaBloom] Control render: unable to read shared volume: %@", String(describing: error))
            return -1
        }
    }

    static func cycle() throws -> Int {
        let defaults = try sharedDefaults()
        let current = currentLevel(defaults: defaults)
        // Start by reducing an initially full level. After muting, return
        // to 100%. One control, no additional gallery clutter.
        let next = current == 0 ? 100 : max(0, current - 25)
        let slider = Double(next) / 100.0
        // Write the EXACT same preferences already used by BloomAudio v0.3.
        defaults.set(slider, forKey: masterKey)
        for guest in guestIDs {
            defaults.set(slider, forKey: "MediaBloomAudioGainV1." + guest)
        }
        // Diagnostic for physical tests: the host displays this tap counter.
        // If it never increases, the App Intent could not access the App Group.
        defaults.set(defaults.integer(forKey: "MediaBloomControlTapCountV1") + 1,
                     forKey: "MediaBloomControlTapCountV1")
        defaults.synchronize()
        NSLog("[MediaBloom] General volume %d%% → %d%%", current, next)
        return next
    }

    static func icon(for level: Int) -> String {
        // SF Symbols stay visible even if the signed extension cannot open
        // the shared group. Never return an empty image or unsupported asset.
        switch level {
        case -1: return "exclamationmark.triangle.fill"
        case 0: return "speaker.slash.fill"
        case 25: return "speaker.fill"
        case 50: return "speaker.wave.1.fill"
        case 75: return "speaker.wave.2.fill"
        default: return "speaker.wave.3.fill"
        }
    }
}

@available(iOSApplicationExtension 18.0, *)
private enum MBControlError: Error, LocalizedError {
    case groupUnavailable
    var errorDescription: String? {
        "MediaBloom no puede acceder a los datos compartidos de SideStore. Revisa que la extensión esté firmada con los mismos App Groups."
    }
}

@available(iOSApplicationExtension 18.0, *)
struct MBGeneralVolumeIntent: AppIntent {
    static let title: LocalizedStringResource = "MediaBloom: Volumen general"
    static let description = IntentDescription("Cambia el volumen de Música y Video sin modificar el volumen del iPhone.")
    static let openAppWhenRun: Bool = false

    func perform() async throws -> some IntentResult {
        _ = try MBMasterVolume.cycle()
        // WidgetKit reloads this control AFTER the AppIntent returns.
        // A premature manual reload here can fetch the old cached state.
        return .result()
    }
}

@available(iOSApplicationExtension 18.0, *)
struct MBGeneralVolumeControl: ControlWidget {
    var body: some ControlWidgetConfiguration {
        // Avoid the asynchronous ControlValueProvider while WidgetKit
        // discovers the extension. Some sideloaded installations exposed
        // an empty/undiscoverable control when the provider touched
        // App Group/Keychain before the control finished registering.
        //
        // The control remains dynamic: WidgetKit reconstructs its content
        // after the action completes; the value comes from BloomAudio's
        // existing shared gain. No extra control is registered.
        StaticControlConfiguration(kind: MBMasterVolume.kind) {
            ControlWidgetButton(action: MBGeneralVolumeIntent()) {
                let value = MBMasterVolume.level()
                Label(value < 0 ? "MediaBloom · Sin acceso" : "MediaBloom \(value)%",
                      systemImage: MBMasterVolume.icon(for: value))
            }
        }
        .displayName("MediaBloom · Volumen")
        .description("Un solo control para Música y Video: 100, 75, 50, 25 y 0%.")
    }
}
