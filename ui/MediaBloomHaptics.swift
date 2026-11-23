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
    // Keep observing across the shared guest player and MediaBloom settings.
    static let shared = MBHapticsDiagnostic()
    @Published private(set) var hardwareAvailable = false
    @Published private(set) var officialActive: Bool? = nil
    @Published private(set) var metadataStatus = ""
    @Published private(set) var testResult = ""
    @Published private(set) var nowPlayingSummary = ""
    @Published private(set) var playbackSummary = ""
    // Capture survives dismissal of Settings. The saved log survives a force-quit.
    @Published private(set) var captureActive = false
    @Published private(set) var recordedEvents: [String] = []
    @Published private(set) var guestEvents: [String] = []
    @Published private(set) var guestSamples = 0
    @Published private(set) var guestLastSample = ""
    private let guestEnabledKey = "MediaBloomHapticsGuestCaptureV1"
    private let guestEventsKey = "MediaBloomHapticsGuestEventsV1"
    private let guestSamplesKey = "MediaBloomHapticsGuestSamplesV1"
    private let guestUpdatedKey = "MediaBloomHapticsGuestUpdatedV1"
    @Published private(set) var lastSample = ""
    @Published private(set) var sampleCount = 0
    private let eventsKey = "MediaBloomHapticsSavedEventsV1"
    private let sampleKey = "MediaBloomHapticsLastSampleV1"
    private let sampleCountKey = "MediaBloomHapticsLastSampleCountV1"
    private var captureTimer: Timer?
    private var lastSongSignature = ""
    private var lastCatalogCheck = ""
    private var statusObserverToken: (any NSCopying)?
    private var engine: CHHapticEngine?
    private var inspectedISRC: String?

    init() {
        hardwareAvailable = CHHapticEngine.capabilitiesForHardware().supportsHaptics
        recordedEvents = UserDefaults.standard.stringArray(forKey: eventsKey) ?? []
        lastSample = UserDefaults.standard.string(forKey: sampleKey) ?? ""
        sampleCount = UserDefaults.standard.integer(forKey: sampleCountKey)
        captureActive = LCUtils.appGroupUserDefault.bool(forKey: guestEnabledKey)
        refreshGuestLog()
    }

    func refreshGuestLog() {
        let shared = LCUtils.appGroupUserDefault
        shared.synchronize()
        guestEvents = shared.stringArray(forKey: guestEventsKey) ?? []
        guestSamples = shared.integer(forKey: guestSamplesKey)
        if let stamp = shared.object(forKey: guestUpdatedKey) as? Date {
            guestLastSample = DateFormatter.localizedString(from: stamp,
                                        dateStyle: .short, timeStyle: .medium)
        } else {
            guestLastSample = ""
        }
    }

    private func saveEvent(_ message: String) {
        let stamp = DateFormatter.localizedString(from: Date(), dateStyle: .none, timeStyle: .medium)
        recordedEvents.append("\(stamp) · \(message)")
        if recordedEvents.count > 32 {
            recordedEvents.removeFirst(recordedEvents.count - 32)
        }
        UserDefaults.standard.set(recordedEvents, forKey: eventsKey)
        // The user must force-quit the LiveContainer host to return to its launcher.
        // Flush each meaningful event rather than assuming normal app termination.
        UserDefaults.standard.synchronize()
    }

    func startCapture(english: Bool) {
        beginObservation(english: english)
        guard !captureActive else { return }
        captureActive = true
        let shared = LCUtils.appGroupUserDefault
        shared.removeObject(forKey: guestEventsKey)
        shared.removeObject(forKey: guestSamplesKey)
        shared.removeObject(forKey: guestUpdatedKey)
        shared.set(true, forKey: guestEnabledKey)
        shared.synchronize()
        refreshGuestLog()
        sampleCount = 0
        lastSongSignature = ""
        lastCatalogCheck = ""
        saveEvent(english ? "Capture started before guest playback." :
                  "Registro iniciado antes de abrir el reproductor.")
        captureNowPlaying(english: english)
        let timer = Timer(timeInterval: 2.0, repeats: true) { [weak self] _ in
            Task { @MainActor [weak self] in
                self?.captureNowPlaying(english: english)
            }
        }
        captureTimer = timer
        RunLoop.main.add(timer, forMode: .common)
    }

    func stopCapture(english: Bool) {
        guard captureActive else { return }
        captureActive = false
        LCUtils.appGroupUserDefault.set(false, forKey: guestEnabledKey)
        LCUtils.appGroupUserDefault.synchronize()
        refreshGuestLog()
        captureTimer?.invalidate()
        captureTimer = nil
        saveEvent(english ? "Capture stopped manually." : "Registro detenido manualmente.")
    }

    func clearCapture() {
        let shared = LCUtils.appGroupUserDefault
        shared.removeObject(forKey: guestEventsKey)
        shared.removeObject(forKey: guestSamplesKey)
        shared.removeObject(forKey: guestUpdatedKey)
        shared.synchronize()
        refreshGuestLog()
        recordedEvents = []
        lastSample = ""
        sampleCount = 0
        UserDefaults.standard.removeObject(forKey: eventsKey)
        UserDefaults.standard.removeObject(forKey: sampleKey)
        UserDefaults.standard.removeObject(forKey: sampleCountKey)
        UserDefaults.standard.synchronize()
    }

    private func captureNowPlaying(english: Bool) {
        guard captureActive else { return }
        guard #available(iOS 18.0, *) else { return }
        // Observe only local Now Playing metadata. Guest metadata may not be
        // accessible; an empty result is not proof no song is playing.
        let info = MPNowPlayingInfoCenter.default().nowPlayingInfo
        let title = (info?[MPMediaItemPropertyTitle] as? String ?? "")
            .trimmingCharacters(in: .whitespacesAndNewlines)
        let artist = (info?[MPMediaItemPropertyArtist] as? String ?? "")
            .trimmingCharacters(in: .whitespacesAndNewlines)
        let isrc = (info?[MPNowPlayingInfoPropertyInternationalStandardRecordingCode] as? String ?? "")
            .trimmingCharacters(in: .whitespacesAndNewlines)
        sampleCount += 1
        // Save a heartbeat too: even if no title arrives, a capture session that
        // kept running in the guest can be distinguished from a stalled session.
        if sampleCount == 1 || sampleCount % 5 == 0 {
            lastSample = DateFormatter.localizedString(from: Date(), dateStyle: .short, timeStyle: .medium)
            UserDefaults.standard.set(lastSample, forKey: sampleKey)
            UserDefaults.standard.set(sampleCount, forKey: sampleCountKey)
            UserDefaults.standard.synchronize()
        }
        let signature = "\(title)|\(artist)|\(isrc)"
        if signature != lastSongSignature {
            lastSongSignature = signature
            if !title.isEmpty || !isrc.isEmpty {
                let song = title.isEmpty ? "(no title)" : title
                let name = artist.isEmpty ? song : "\(song) — \(artist)"
                saveEvent("Now Playing: \(name) | ISRC: \(isrc.isEmpty ? "missing" : isrc)")
            }
        }
        guard !isrc.isEmpty, isrc != lastCatalogCheck else { return }
        lastCatalogCheck = isrc
        MAMusicHapticsManager.shared.checkHapticTrackAvailabilityForMedia(matchingCode: isrc) { [weak self] available in
            Task { @MainActor [weak self] in
                guard let self, self.captureActive, self.lastCatalogCheck == isrc else { return }
                self.saveEvent(available ? "Apple haptic track available for \(isrc)" :
                                          "Apple haptic track unavailable for \(isrc)")
            }
        }
    }

    // Apple reports actual haptic track playback here, not just support/setting.
    // This observer only monitors; it never changes playback or Now Playing data.
    func beginObservation(english: Bool) {
        if #available(iOS 18.0, *), statusObserverToken == nil {
            statusObserverToken = MAMusicHapticsManager.shared.addStatusObserver { [weak self] code, playing in
                Task { @MainActor [weak self] in
                    guard let self else { return }
                    if playing {
                        if self.captureActive {
                            self.saveEvent("Apple Music Haptics ACTIVE for ISRC \(code)")
                        }
                        self.playbackSummary = english
                            ? "iOS CONFIRMED synchronized Music Haptics for ISRC: \(code)"
                            : "iOS CONFIRMÓ vibraciones sincronizadas para ISRC: \(code)"
                    } else {
                        if self.captureActive {
                            self.saveEvent("Apple Music Haptics stopped (ISRC \(code))")
                        }
                        self.playbackSummary = english
                            ? "iOS reported that the haptic track stopped."
                            : "iOS informó que la pista háptica se detuvo."
                    }
                }
            }
        }
        if playbackSummary.isEmpty {
            playbackSummary = english
                ? "No confirmed synchronized haptic playback yet. The three-tap test does not count."
                : "Aún no hay confirmación de vibraciones sincronizadas. La prueba de tres toques no cuenta."
        }
        refresh(english: english)
    }

    func refresh(english: Bool) {
        refreshGuestLog()
        hardwareAvailable = CHHapticEngine.capabilitiesForHardware().supportsHaptics
        officialActive = nil
        inspectedISRC = nil
        nowPlayingSummary = english
            ? "The app has not provided visible Now Playing metadata to this diagnostic."
            : "Esta prueba aún no ve datos de la canción que se está reproduciendo."
        if #available(iOS 18.0, *) {
            let manager = MAMusicHapticsManager.shared
            officialActive = manager.isActive
            // The guest may not register Now Playing, and it would be dishonest
            // to post fake metadata that claims MediaBloom is playing the song.
            let info = MPNowPlayingInfoCenter.default().nowPlayingInfo
            let title = (info?[MPMediaItemPropertyTitle] as? String ?? "")
                .trimmingCharacters(in: .whitespacesAndNewlines)
            let artist = (info?[MPMediaItemPropertyArtist] as? String ?? "")
                .trimmingCharacters(in: .whitespacesAndNewlines)
            if !title.isEmpty {
                nowPlayingSummary = artist.isEmpty ? title : "\(title) — \(artist)"
            }
            let rawCode = info?[MPNowPlayingInfoPropertyInternationalStandardRecordingCode] as? String ?? ""
            let code = rawCode.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !code.isEmpty else {
                metadataStatus = !title.isEmpty
                    ? (english
                       ? "The song title is visible, but this player has not supplied an ISRC. Apple cannot match its haptic track from these details alone."
                       : "Vemos el título de la canción, pero el reproductor no entregó su ISRC. Apple no puede identificar la pista háptica solo con el título.")
                    : (english
                       ? "No song title or ISRC is exposed to the MediaBloom host. Opening a guest does not automatically publish this information."
                       : "MediaBloom no recibe título ni ISRC del reproductor. Abrir una app compartida no transmite estos datos automáticamente.")
                return
            }
            inspectedISRC = code
            metadataStatus = english
                ? "ISRC received (\(code)). Checking Apple's haptic catalog…"
                : "ISRC recibido (\(code)). Consultando el catálogo háptico de Apple…"
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
