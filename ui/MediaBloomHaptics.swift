// MediaBloom music-haptics feasibility probe (iOS 15+ host, iOS 18+ Apple Music Haptics).
// Music Haptics is SYSTEM-DRIVEN. Declaring MusicHapticsSupported never means
// that a guest streaming app supplies the ISRC Now Playing metadata Apple needs.
// No microphone, audio capture, swizzles, or fabricated music synchronization.

import Foundation
import SwiftUI
import UIKit
import CoreHaptics
import MediaAccessibility
import MediaPlayer

@MainActor
final class MBHapticsDiagnostic: ObservableObject {
    @Published private(set) var hardwareAvailable = false
    @Published private(set) var officialActive: Bool? = nil
    @Published private(set) var metadataStatus = ""
    @Published private(set) var testResult = ""
    private var engine: CHHapticEngine?
    private var inspectedISRC: String?

    init() {
        hardwareAvailable = CHHapticEngine.capabilitiesForHardware().supportsHaptics
    }

    func refresh(english: Bool) {
        hardwareAvailable = CHHapticEngine.capabilitiesForHardware().supportsHaptics
        officialActive = nil
        inspectedISRC = nil
        if #available(iOS 18.0, *) {
            let manager = MAMusicHapticsManager.shared
            officialActive = manager.isActive
            // The guest may not register Now Playing, and it would be dishonest
            // to post fake metadata that claims MediaBloom is playing the song.
            let info = MPNowPlayingInfoCenter.default().nowPlayingInfo
            guard let code = info?[MPNowPlayingInfoPropertyInternationalStandardRecordingCode] as? String,
                  !code.isEmpty else {
                metadataStatus = english
                    ? "No song ISRC exposed to MediaBloom. Automatic Music Haptics cannot be confirmed for this shared player."
                    : "MediaBloom no recibe el código ISRC de la canción. No podemos confirmar vibraciones automáticas en este reproductor."
                return
            }
            inspectedISRC = code
            metadataStatus = english ? "Checking song availability…" : "Comprobando disponibilidad de la canción…"
            manager.checkHapticTrackAvailabilityForMedia(matchingCode: code) { [weak self] available in
                Task { @MainActor [weak self] in
                    guard let self, self.inspectedISRC == code else { return }
                    self.metadataStatus = available
                        ? (english ? "Apple has a haptic track for the reported song." :
                                      "Apple tiene una pista háptica para esta canción.")
                        : (english ? "Apple does not report a haptic track for this song." :
                                      "Apple no reporta una pista háptica para esta canción.")
                }
            }
        } else {
            metadataStatus = english
                ? "Apple Music Haptics requires iOS 18 or later."
                : "Music Haptics de Apple requiere iOS 18 o posterior."
        }
    }

    func testHardware(english: Bool) {
        guard hardwareAvailable else {
            testResult = english ? "This device does not support Core Haptics." :
                                   "Este dispositivo no admite Core Haptics."
            return
        }
        do {
            // Three short physical test taps ONLY; not synchronized to any song.
            // No repeating timer, no background haptics and no audio-session changes.
            engine?.stop(completionHandler: nil)
            let engine = try CHHapticEngine()
            engine.playsHapticsOnly = true
            try engine.start()
            let events: [CHHapticEvent] = (0..<3).map { index in
                CHHapticEvent(eventType: .hapticTransient, parameters: [
                    CHHapticEventParameter(parameterID: .hapticIntensity, value: 0.7),
                    CHHapticEventParameter(parameterID: .hapticSharpness, value: 0.45)
                ], relativeTime: Double(index) * 0.24)
            }
            let pattern = try CHHapticPattern(events: events, parameters: [])
            let player = try engine.makePlayer(with: pattern)
            try player.start(atTime: CHHapticTimeImmediate)
            self.engine = engine
            testResult = english ? "Sent three test taps? Core Haptics is working." :
                                   "¿Sentiste tres toques? El motor háptico funciona."
        } catch {
            testResult = (english ? "Haptic test failed: " : "Falló la prueba háptica: ") +
                          error.localizedDescription
        }
    }
}
