#!/usr/bin/env python3
"""Overlay experimental BloomAudio onto a throwaway pinned LC source checkout.

This script is NOT invoked by the BloomBox project's build. Only MediaBloom
Actions calls it. No primary BloomBox source code or IPA is modified.
"""
from pathlib import Path

ROOT = Path(".")
HOOK = (Path("../media-tools/audio/BloomAudioRuntime.inc").read_text()
        + "\n"
        + Path("../media-tools/audio/BloomAudioOverlay.inc").read_text())
LOADER = ROOT / "TweakLoader/TweakLoader.m"
SWIFT = ROOT / "LiveContainerSwiftUI/Views/BloomBox/BloomBoxLauncherRoot.swift"


def replace_once(data: str, before: str, after: str, name: str) -> str:
    count = data.count(before)
    if count != 1:
        raise RuntimeError(f"{name}: expected one match, got {count}")
    return data.replace(before, after, 1)


def main():
    loader = LOADER.read_text()
    loader = replace_once(
        loader,
        " __attribute__((constructor))\nstatic void TweakLoaderConstructor()",
        HOOK + "\n __attribute__((constructor))\nstatic void TweakLoaderConstructor()",
        "TweakLoader constructor anchor",
    )
    loader = replace_once(
        loader,
        '        NSLog(@"Skip loading tweaks");\n        return;\n    }\n',
        '        NSLog(@"Skip loading tweaks");\n        return;\n    }\n\n'
        '    // Only MediaBloom and opted-in YouTube guests activate this hook.\n'
        '    MBInstallAudioHooks();\n',
        "TweakLoader injection call",
    )
    LOADER.write_text(loader)

    swift = SWIFT.read_text()
    swift = replace_once(
        swift,
        "import SwiftUI\n",
        "import SwiftUI\nimport WidgetKit\n",
        "Add WidgetKit for MediaBloom controls",
    )
    swift = replace_once(
        swift,
        '    @State private var searchText = ""\n',
        '    @State private var searchText = ""\n'
        '    @AppStorage("MediaBloomAudioProbeEnabledV1", store: LCUtils.appGroupUserDefault)\n'
        '    private var audioProbeEnabled = false\n',
        "Media Home enabled switch",
    )
    swift = replace_once(
        swift,
        '                    .padding(.top, 20)\n\n'
        '                    HStack(spacing: 10) {\n',
        '                    .padding(.top, 20)\n\n'
        '                    VStack(alignment: .leading, spacing: 8) {\n'
        '                        Toggle(isOn: $audioProbeEnabled) {\n'
        '                            Label(english ? "BloomAudio (experimental)" : "BloomAudio (experimental)",\n'
        '                                  systemImage: "waveform")\n'
        '                                .font(.subheadline.weight(.semibold))\n'
        '                        }\n'
        '                        .tint(.purple)\n'
        '                        Text(english\n'
        '                             ? "Separate volume for YouTube and YouTube Music. Reopen the app after enabling, then use the floating speaker button during playback."\n'
        '                             : "Volumen independiente para YouTube y YouTube Music. Actívalo, vuelve a abrir la app y usa el botón flotante del altavoz mientras reproduce.")\n'
        '                            .font(.caption)\n'
        '                            .foregroundStyle(.secondary)\n'
        '                    }\n'
        '                    .padding(14)\n'
        '                    .background(Color.white.opacity(0.055),\n'
        '                                in: RoundedRectangle(cornerRadius: 16))\n\n'
        '                    HStack(spacing: 10) {\n',
        "Media Home toggle insertion",
    )
    swift = replace_once(
        swift,
        '                                    .buttonStyle(.plain)\n'
        '                                }\n'
        '                            }\n'
        '                        }\n'
        '                    }\n\n'
        '                    Text(english\n',
        '                                    .buttonStyle(.plain)\n'
        '                                    if audioProbeEnabled {\n'
        '                                        BloomBoxMediaAudioSlider(app: app, english: english)\n'
        '                                    }\n'
        '                                }\n'
        '                            }\n'
        '                        }\n'
        '                    }\n\n'
        '                    Text(english\n',
        "Media Home per-app sliders",
    )
    swift += r'''

// MARK: - Experimental per-app volume control (shared defaults with the guest)
//
// The iOS device volume is never changed by this UI. Hooks are opt-in and can
// be disabled without touching installed guest applications or their data.
private struct BloomBoxMediaAudioSlider: View {
    let app: LCAppModel
    let english: Bool
    @State private var gain: Double

    init(app: LCAppModel, english: Bool) {
        self.app = app
        self.english = english
        let key = "MediaBloomAudioGainV1." + app.bundleIdentifier.lowercased()
        let stored = LCUtils.appGroupUserDefault.object(forKey: key) as? NSNumber
        _gain = State(initialValue: max(0, min(1, stored?.doubleValue ?? 1)))
    }

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: gain == 0 ? "speaker.slash.fill" : "speaker.wave.2.fill")
                .foregroundStyle(.purple)
            Slider(value: $gain, in: 0...1, step: 0.05) { editing in
                // Save every move, but only ask iOS to refresh the Control
                // Center glyph after finishing a drag (no reload storm).
                if !editing { persistVolume(refreshControl: true) }
            }
            .tint(.purple)
            .onChange(of: gain) { _ in persistVolume() }
            Text("\(Int((gain * 100).rounded()))%")
                .font(.caption.monospacedDigit())
                .foregroundStyle(.secondary)
                .frame(width: 40, alignment: .trailing)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .background(Color.white.opacity(0.035),
                    in: RoundedRectangle(cornerRadius: 13))
        .accessibilityLabel(english ? "Experimental app volume" : "Volumen experimental de la app")
    }

    private func persistVolume(refreshControl: Bool = false) {
        let shared = LCUtils.appGroupUserDefault
        let value = max(0, min(1, gain))
        let key = "MediaBloomAudioGainV1." + app.bundleIdentifier.lowercased()
        shared.set(value, forKey: key)
        // The general Control Center button represents the most recently
        // changed MediaBloom level, even when edited inside MediaBloom.
        shared.set(value, forKey: "MediaBloomMasterGainV1")
        shared.synchronize()
        if refreshControl { MediaBloomControlCenterSync.notify() }
    }
}

// Swift-to-Objective-C bridge, called by BloomAudio's native guest overlay.
// Explicit @objc runtime name keeps the bridge stable across Swift modules.
@objc(MediaBloomControlCenterSync)
final class MediaBloomControlCenterSync: NSObject {
    @objc static func notify() {
        if #available(iOS 18.0, *) {
            ControlCenter.shared.reloadControls(
                ofKind: "com.kdt.bloomboxmedia.master.volume")
        }
    }
}
'''
    SWIFT.write_text(swift)
    assert 'MediaBloomAudioProbeEnabledV1' in loader and 'MediaBloomAudioProbeEnabledV1' in swift
    assert "MBInstallAudioHooks();" in loader
    assert "MBStartVolumeOverlay();" in loader
    assert "MBVolumePassThroughWindow" in loader
    assert "BloomBoxMediaAudioSlider(app: app, english: english)" in swift
    print("PASS: opt-in BloomAudio runtime and per-app sliders applied to Media-only source")


if __name__ == "__main__":
    main()
