// MediaBloom appearance customization. Kept entirely within the MediaBloom host.
// No changes to guest app files, SideStore containers or iOS wallpaper.
import SwiftUI
import UIKit
import PhotosUI
import UniformTypeIdentifiers

struct MBReferenceTheme {
    let coral: Color
    let background: Color
    let white: Color
    let gray: Color
    let ink = Color(red: 0.097, green: 0.101, blue: 0.113)
    let track = Color(red: 0.874, green: 0.870, blue: 0.860)
    let pill = Color(red: 0.890, green: 0.891, blue: 0.879)

    init(accent: Color, background: Color, white: Color, gray: Color) {
        self.coral = accent
        self.background = background
        self.white = white
        self.gray = gray
    }
}

// Native LIGHT frosted glass with a subtle color wash: wallpaper stays visible.
// Uses only iOS 15-compatible materials and never changes the system wallpaper.
struct MBGlassPanel: View {
    let tint: Color
    let opacity: Double
    let cornerRadius: CGFloat

    var body: some View {
        let shape = RoundedRectangle(cornerRadius: cornerRadius, style: .continuous)
        let amount = max(0, min(1, opacity))
        return ZStack {
            // At 0% no material, wash, shadow, or stroke remains; background
            // really is transparent. Keep the material light in dark MediaBloom.
            if amount > 0.001 {
                shape.fill(.ultraThinMaterial)
                    .environment(\.colorScheme, .light)
                    .opacity(amount * 0.90)
                shape.fill(tint.opacity(amount * 0.22))
                shape.stroke(Color.white.opacity(amount * 0.42), lineWidth: 0.8)
            }
        }
        .clipShape(shape)
    }
}

enum MBColorHex {
    static func color(_ text: String) -> Color {
        let clean = text.trimmingCharacters(in: .whitespacesAndNewlines)
            .replacingOccurrences(of: "#", with: "")
        guard clean.count == 6, let rgb = UInt64(clean, radix: 16) else {
            return Color(red: 1, green: 0.535, blue: 0.415)
        }
        return Color(red: Double((rgb >> 16) & 255) / 255,
                     green: Double((rgb >> 8) & 255) / 255,
                     blue: Double(rgb & 255) / 255)
    }

    static func dark(_ text: String) -> Bool {
        let normalized = text.replacingOccurrences(of: "#", with: "")
        guard let rgb = UInt64(normalized, radix: 16), normalized.count == 6 else { return false }
        let r = Double((rgb >> 16) & 255) / 255
        let g = Double((rgb >> 8) & 255) / 255
        let b = Double(rgb & 255) / 255
        return r * 0.2126 + g * 0.7152 + b * 0.0722 < 0.45
    }

    static func hex(_ color: Color) -> String {
        let rgba = UIColor(color).resolvedColor(with: UITraitCollection(userInterfaceStyle: .light))
        var red: CGFloat = 0, green: CGFloat = 0, blue: CGFloat = 0, alpha: CGFloat = 0
        guard rgba.getRed(&red, green: &green, blue: &blue, alpha: &alpha) else {
            return "FF896A"
        }
        return String(format: "%02X%02X%02X", Int(round(red * 255)),
                      Int(round(green * 255)), Int(round(blue * 255)))
    }
}

extension Notification.Name {
    static let mediaBloomWallpaperDidChange = Notification.Name("MediaBloomWallpaperDidChange")
    static let mediaBloomMediaTagsDidChange = Notification.Name("MediaBloomMediaTagsDidChange")
}

enum MBWallpaperStore {
    private static var imageURL: URL? {
        guard let support = FileManager.default.urls(for: .applicationSupportDirectory,
                                                     in: .userDomainMask).first else { return nil }
        return support.appendingPathComponent("MediaBloom", isDirectory: true)
            .appendingPathComponent("wallpaper.jpg")
    }

    static func load() -> UIImage? {
        guard let imageURL else { return nil }
        return UIImage(contentsOfFile: imageURL.path)
    }

    @discardableResult
    static func save(_ image: UIImage) -> Bool {
        guard let imageURL else { return false }
        let size = image.size
        guard size.width > 0, size.height > 0 else { return false }
        let scale = min(1, 1800 / max(size.width, size.height))
        let outputSize = CGSize(width: max(1, size.width * scale),
                                height: max(1, size.height * scale))
        let renderer = UIGraphicsImageRenderer(size: outputSize)
        let smaller = renderer.image { _ in
            image.draw(in: CGRect(origin: .zero, size: outputSize))
        }
        guard let data = smaller.jpegData(compressionQuality: 0.82) else { return false }
        do {
            try FileManager.default.createDirectory(at: imageURL.deletingLastPathComponent(),
                                                    withIntermediateDirectories: true)
            try data.write(to: imageURL, options: .atomic)
            return true
        } catch {
            NSLog("[MediaBloom] Wallpaper save failed: %@", String(describing: error))
            return false
        }
    }

    static func remove() {
        if let imageURL { try? FileManager.default.removeItem(at: imageURL) }
    }

    static func notifyChanged() {
        NotificationCenter.default.post(name: .mediaBloomWallpaperDidChange, object: nil)
    }
}

private struct MBPhotosSelection: UIViewControllerRepresentable {
    let onSelect: (UIImage) -> Void

    func makeUIViewController(context: Context) -> PHPickerViewController {
        var configuration = PHPickerConfiguration(photoLibrary: .shared())
        configuration.filter = .images
        configuration.selectionLimit = 1
        let picker = PHPickerViewController(configuration: configuration)
        picker.delegate = context.coordinator
        return picker
    }

    func updateUIViewController(_ uiViewController: PHPickerViewController, context: Context) {}

    func makeCoordinator() -> Coordinator { Coordinator(onSelect: onSelect) }

    final class Coordinator: NSObject, PHPickerViewControllerDelegate {
        let onSelect: (UIImage) -> Void
        init(onSelect: @escaping (UIImage) -> Void) { self.onSelect = onSelect }

        func picker(_ picker: PHPickerViewController, didFinishPicking results: [PHPickerResult]) {
            picker.dismiss(animated: true)
            guard let item = results.first?.itemProvider, item.canLoadObject(ofClass: UIImage.self) else { return }
            item.loadObject(ofClass: UIImage.self) { [onSelect] object, _ in
                guard let image = object as? UIImage else { return }
                DispatchQueue.main.async { onSelect(image) }
            }
        }
    }
}

struct MediaBloomAppearanceSheet: View {
    let onOpenAdvanced: () -> Void
    let onManageMediaLabels: () -> Void
    @Environment(\.dismiss) private var dismiss
    @AppStorage("BloomBoxPreferredLanguage", store: LCUtils.appGroupUserDefault)
    private var language = "system"
    @AppStorage("MediaBloomAccentHexV1") private var accent = "FF896A"
    @AppStorage("MediaBloomBackgroundHexV1") private var background = "0E0F11"
    @AppStorage("MediaBloomCardHexV1") private var card = "F5F4F1"
    @AppStorage("MediaBloomMixCardHexV1") private var mix = "CBCDC9"
    @AppStorage("MediaBloomWallpaperEnabledV1") private var enabled = true
    @AppStorage("MediaBloomWallpaperBlurV1") private var blur = 14.0
    @AppStorage("MediaBloomGlassOpacityV1") private var glassOpacity = 0.35
    @AppStorage("MediaBloomGlassTintHexV1") private var glassTint = "F5F4F1"
    @AppStorage("MediaBloomAudioProbeEnabledV1", store: LCUtils.appGroupUserDefault)
    private var audioEnabled = false
    @AppStorage("MediaBloomEdgeEnabledV1", store: LCUtils.appGroupUserDefault)
    private var edgeEnabled = true
    @AppStorage("MediaBloomEdgeOpacityV1", store: LCUtils.appGroupUserDefault)
    private var edgeOpacity = 1.0
    @AppStorage("MediaBloomEdgeRightV1", store: LCUtils.appGroupUserDefault)
    private var edgeRight = true
    @State private var showEdgeAppPicker = false
    @State private var showPhotos = false
    @State private var showFiles = false
    @State private var hasWallpaper = false
    @State private var imageError = false
    @StateObject private var haptics = MBHapticsDiagnostic.shared
    @AppStorage("MediaBloomHapticsAutoISRCV1", store: LCUtils.appGroupUserDefault)
    private var automaticMusicHaptics = false
    @AppStorage("MediaBloomHapticsGuestOverlayV1", store: LCUtils.appGroupUserDefault)
    private var showHapticsInPlayer = true

    private var english: Bool {
        language == "en" || (language == "system" &&
            !(Locale.preferredLanguages.first ?? "es").lowercased().hasPrefix("es"))
    }
    private func t(_ es: String, _ en: String) -> String { english ? en : es }

    var body: some View {
        NavigationView {
            Form {
                wallpaperSection
                paletteSection
                glassSection
                mediaTagsSection
                audioSection
                edgeSection
                hapticsSection
                advancedSection
            }
            .navigationTitle(t("Apariencia", "Appearance"))
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button(t("Listo", "Done")) { dismiss() }
                }
            }
            .onAppear {
                hasWallpaper = MBWallpaperStore.load() != nil
                haptics.beginObservation(english: english)
            }
            .onReceive(NotificationCenter.default.publisher(for: UIApplication.didBecomeActiveNotification)) { _ in
                haptics.refresh(english: english)
            }
            .sheet(isPresented: $showEdgeAppPicker) {
                MBEdgeAppPicker(english: english)
            }
            .sheet(isPresented: $showPhotos) {
                MBPhotosSelection { image in saveWallpaper(image) }
            }
            .fileImporter(isPresented: $showFiles, allowedContentTypes: [.image]) { result in
                guard case .success(let url) = result else { return }
                let scoped = url.startAccessingSecurityScopedResource()
                defer { if scoped { url.stopAccessingSecurityScopedResource() } }
                if let data = try? Data(contentsOf: url), let image = UIImage(data: data) {
                    saveWallpaper(image)
                } else { imageError = true }
            }
            .alert(t("No se pudo guardar la imagen", "Could not save image"), isPresented: $imageError) {
                Button("OK", role: .cancel) {}
            }
        }
        .navigationViewStyle(.stack)
        .preferredColorScheme(.light)
    }


    private var wallpaperSection: some View {
        Section(header: Text(t("Fondo de pantalla", "Wallpaper"))) {
            HStack(spacing: 12) {
                Button { showPhotos = true } label: {
                    Label(t("Fotos", "Photos"), systemImage: "photo.on.rectangle")
                }
                Button { showFiles = true } label: {
                    Label(t("Archivos", "Files"), systemImage: "folder")
                }
            }
            Toggle(t("Mostrar fondo", "Show wallpaper"), isOn: $enabled)
                .disabled(!hasWallpaper)
            VStack(alignment: .leading) {
                HStack {
                    Text(t("Desenfoque", "Blur"))
                    Spacer()
                    Text("\(Int(blur))")
                }
                Slider(value: $blur, in: 0...40, step: 1)
            }
            .disabled(!enabled || !hasWallpaper)
            if hasWallpaper {
                Button(role: .destructive) {
                    MBWallpaperStore.remove()
                    hasWallpaper = false
                    enabled = false
                    MBWallpaperStore.notifyChanged()
                } label: { Label(t("Quitar fondo", "Remove wallpaper"), systemImage: "trash") }
            }
            Text(t("La imagen se guarda solo en MediaBloom, sin modificar el fondo del iPhone.",
                   "The image stays in MediaBloom and does not change your iPhone wallpaper."))
                .font(.footnote).foregroundColor(.secondary)
        }
    }

    private var paletteSection: some View {
        Section(header: Text(t("Colores predominantes", "Main colors"))) {
            ColorPicker(t("Color principal", "Accent"), selection: Binding(
                get: { MBColorHex.color(accent) },
                set: { accent = MBColorHex.hex($0) }), supportsOpacity: false)
            ColorPicker(t("Fondo", "Background"), selection: Binding(
                get: { MBColorHex.color(background) },
                set: { background = MBColorHex.hex($0) }), supportsOpacity: false)
            ColorPicker(t("Tarjetas claras", "Light cards"), selection: Binding(
                get: { MBColorHex.color(card) },
                set: { card = MBColorHex.hex($0) }), supportsOpacity: false)
            ColorPicker(t("Tarjeta de música", "Music card"), selection: Binding(
                get: { MBColorHex.color(mix) },
                set: { mix = MBColorHex.hex($0) }), supportsOpacity: false)
            Button(t("Restaurar colores originales", "Restore default colors")) {
                accent = "FF896A"; background = "0E0F11"
                card = "F5F4F1"; mix = "CBCDC9"
                glassOpacity = 0.35; glassTint = "F5F4F1"
            }
        }
    }

    private var glassSection: some View {
        Section(header: Text(t("Transparencia de tarjetas", "Card transparency"))) {
            ColorPicker(t("Color del cristal", "Glass color"),
                        selection: Binding(
                            get: { MBColorHex.color(glassTint) },
                            set: { glassTint = MBColorHex.hex($0) }),
                        supportsOpacity: false)

            VStack(alignment: .leading, spacing: 9) {
                HStack {
                    Text(t("Efecto cristal", "Frosted glass"))
                    Spacer()
                    Text("\(Int((glassOpacity * 100).rounded()))%")
                        .monospacedDigit()
                        .foregroundColor(.secondary)
                }
                Slider(value: $glassOpacity, in: 0...1, step: 0.01)
                    .tint(MBColorHex.color(accent))
                HStack {
                    Text(t("0% · Invisible", "0% · Invisible"))
                    Spacer()
                    Text(t("Más sólido", "More solid"))
                }
                .font(.caption2)
                .foregroundColor(.secondary)
            }
            Text(t("Los paneles difuminan el fondo, pero mantienen los nombres y botones legibles.",
                   "Panels blur the wallpaper while keeping labels and buttons readable."))
                .font(.footnote)
                .foregroundColor(.secondary)
        }
    }

    private var edgeSection: some View {
        Section(header: Text("MediaBloom Edge")) {
            Toggle(t("Menú lateral", "Edge menu"), isOn: $edgeEnabled)
            Toggle(t("Borde derecho", "Right edge"), isOn: $edgeRight)
            Button { showEdgeAppPicker = true } label: {
                Label(t("Elegir apps de la muesca", "Choose Edge apps"),
                      systemImage: "square.grid.2x2")
            }
            Text(t("Elige hasta 4 aplicaciones. Puedes cambiar el orden desde la lista.",
                   "Choose up to 4 apps. You can reorder them from the list."))
                .font(.footnote).foregroundColor(.secondary)
            HStack {
                Text(t("Opacidad", "Opacity"))
                Spacer()
                Text("\(Int((edgeOpacity * 100).rounded()))%").monospacedDigit()
            }
            Slider(value: $edgeOpacity, in: 0...1, step: 0.01)
            Button(t("Restaurar negro puro", "Restore pure black")) { edgeOpacity = 1 }
            Text(t("0%: transparente. 100%: negro puro. Los botones siguen visibles. Vuelve a abrir el reproductor para aplicar los ajustes.",
                   "0%: transparent. 100%: pure black. Buttons remain visible. Reopen the player to apply settings."))
                .font(.footnote).foregroundColor(.secondary)
        }
    }

    private var audioSection: some View {
        Section(header: Text("BloomAudio")) {
            Toggle(t("Activar control de audio", "Enable audio control"), isOn: $audioEnabled)
            Text(t("Después de activarlo, vuelve a abrir las apps multimedia. El volumen general se combina con el volumen individual.",
                   "Reopen media apps after enabling. Master and individual gain combine."))
                .font(.footnote).foregroundColor(.secondary)
        }
    }

    private var hapticsSection: some View {
        Section(header: Text(t("Música háptica · Prueba", "Music Haptics · Test"))) {
            HStack(spacing: 10) {
                Image(systemName: haptics.hardwareAvailable ? "waveform" : "waveform.slash")
                    .foregroundColor(haptics.hardwareAvailable ? .green : .secondary)
                Text(haptics.hardwareAvailable
                     ? t("Motor de vibración disponible", "Haptic engine available")
                     : t("Motor de vibración no disponible", "Haptic engine unavailable"))
            }
            if let active = haptics.officialActive {
                Label(active
                      ? t("Music Haptics de iOS: activado", "iOS Music Haptics: enabled")
                      : t("Music Haptics de iOS: desactivado", "iOS Music Haptics: disabled"),
                      systemImage: active ? "checkmark.circle" : "circle")
            } else {
                Text(t("La función oficial requiere iOS 18 o posterior.",
                       "Apple Music Haptics requires iOS 18 or later."))
                    .font(.footnote)
                    .foregroundColor(.secondary)
            }
            Text(haptics.nowPlayingSummary)
                .font(.footnote)
                .foregroundColor(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            Text(haptics.metadataStatus)
                .font(.footnote)
                .foregroundColor(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            Label(haptics.playbackSummary, systemImage: "waveform.path.ecg")
                .font(.footnote)
                .foregroundColor(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            Button {
                if haptics.captureActive {
                    haptics.stopCapture(english: english)
                } else {
                    haptics.startCapture(english: english)
                }
            } label: {
                Label(haptics.captureActive
                      ? t("Detener registro", "Stop capture")
                      : t("Iniciar registro para YouTube Music", "Start YouTube Music capture"),
                      systemImage: haptics.captureActive ? "stop.circle.fill" : "record.circle")
            }
            Toggle(isOn: $automaticMusicHaptics) {
                Label(t("Probar Music Haptics automático",
                        "Try automatic Apple Music Haptics"), systemImage: "waveform")
            }
            .disabled(!haptics.hardwareAvailable)
            Toggle(isOn: $showHapticsInPlayer) {
                Label(t("Mostrar estado en el reproductor",
                        "Show haptic status inside player"), systemImage: "waveform.path")
            }
            .disabled(!haptics.hardwareAvailable)
            Text(t("En YouTube Music aparecerá una barra pequeña con el estado de carga y un botón ↻ para resincronizar sin detener la canción. Apple no muestra un porcentaje de descarga háptica.",
                   "YouTube Music shows a compact loading indicator and ↻ button to retry synchronization without stopping the song. Apple does not expose haptic download progress."))
                .font(.caption)
                .foregroundColor(.secondary)
            Text(t("Experimental: busca la grabación exacta con MusicBrainz y solo añade su ISRC cuando Apple confirma una pista háptica. Activa también el registro y vuelve a abrir YouTube Music.",
                   "Experimental: looks up the exact recording in MusicBrainz and adds its ISRC only when Apple reports a haptic track. Also start capture and reopen YouTube Music."))
                .font(.caption)
                .foregroundColor(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            if !haptics.guestHapticsState.isEmpty {
                Label(t("Último resultado de Music Haptics",
                        "Last Music Haptics result"),
                      systemImage: haptics.guestHapticsSymbol)
                    .font(.subheadline.weight(.semibold))
                Text(haptics.guestHapticsLabel(english: english))
                    .font(.headline)
                if !haptics.guestHapticsSong.isEmpty {
                    Text(haptics.guestHapticsSong)
                        .font(.footnote)
                }
                if !haptics.guestHapticsDetail.isEmpty {
                    Text(haptics.guestHapticsDetail)
                        .font(.caption)
                        .foregroundColor(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                }
                if !haptics.guestHapticsDate.isEmpty {
                    Text(t("Registrado durante la reproducción: ",
                           "Recorded during playback: ") + haptics.guestHapticsDate)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }
            if haptics.guestRejectedCount > 0 {
                Text(t("Códigos descartados temporalmente: ",
                       "Temporarily rejected codes: ")
                     + "\(haptics.guestRejectedCount)")
                    .font(.footnote)
                    .foregroundColor(.secondary)
                Button {
                    haptics.retryRejectedISRCs()
                } label: {
                    Label(t("Reintentar códigos descartados",
                            "Retry previously rejected codes"),
                          systemImage: "arrow.clockwise")
                }
            }
            if haptics.guestSamples > 0 || !haptics.guestEvents.isEmpty {
                Label(t("Diagnóstico desde YouTube Music", "YouTube Music guest diagnostic"),
                      systemImage: "waveform.path.ecg")
                    .font(.subheadline.weight(.semibold))
                Text(t("Lecturas dentro del reproductor: ", "Samples inside player: ")
                     + "\(haptics.guestSamples)")
                    .font(.footnote.monospacedDigit())
                if !haptics.guestLastSample.isEmpty {
                    Text(t("Última lectura del reproductor: ", "Last guest sample: ")
                         + haptics.guestLastSample)
                        .font(.footnote)
                        .foregroundColor(.secondary)
                }
                Text(haptics.guestEvents.suffix(12).joined(separator: "\n"))
                    .font(.caption2)
                    .foregroundColor(.secondary)
                    .textSelection(.enabled)
                    .fixedSize(horizontal: false, vertical: true)
            } else {
                Text(t("Sin lecturas desde YouTube Music. Inicia el registro y abre el reproductor desde MediaBloom.",
                       "No in-guest data yet. Start capture, then open YouTube Music from MediaBloom."))
                    .font(.footnote)
                    .foregroundColor(.secondary)
            }
            if !haptics.lastSample.isEmpty {
                Text(t("Última revisión del launcher: ", "Last launcher sample: ") + haptics.lastSample
                     + " (\(haptics.sampleCount))")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            if !haptics.recordedEvents.isEmpty || !haptics.guestEvents.isEmpty {
                Text(t("Registro conservado después de cerrar MediaBloom", "Log preserved across force-close"))
                    .font(.footnote.weight(.semibold))
                Text(haptics.recordedEvents.suffix(8).joined(separator: "\n"))
                    .font(.caption2)
                    .foregroundColor(.secondary)
                    .textSelection(.enabled)
                    .fixedSize(horizontal: false, vertical: true)
                Button {
                    UIPasteboard.general.string = (haptics.guestEvents
                        + haptics.recordedEvents).joined(separator: "\n")
                } label: {
                    Label(t("Copiar registro", "Copy log"), systemImage: "doc.on.doc")
                }
                Button(role: .destructive) {
                    haptics.clearCapture()
                } label: {
                    Label(t("Borrar registro", "Clear log"), systemImage: "trash")
                }
            }
            Text(t("Music Haptics revisa la canción cada medio segundo. Los códigos rechazados se recuerdan durante 12 horas; después se pueden comprobar de nuevo. Busca en MusicBrainz y Deezer sin grabar audio ni usar el micrófono.",
                   "Music Haptics checks song metadata every half-second. Rejected codes are remembered for 12 hours, then retried. It uses MusicBrainz and Deezer without recording audio or using the microphone."))
                .font(.caption2)
                .foregroundColor(.secondary)
            Button {
                haptics.testHardware(english: english)
            } label: {
                Label(t("Probar 3 vibraciones", "Test 3 haptic taps"),
                      systemImage: "waveform.path")
            }
            .disabled(!haptics.hardwareAvailable)
            if !haptics.testResult.isEmpty {
                Text(haptics.testResult)
                    .font(.footnote)
                    .foregroundColor(.secondary)
            }
            Button {
                haptics.refresh(english: english)
            } label: {
                Label(t("Volver a comprobar", "Check again"), systemImage: "arrow.clockwise")
            }
            Text(t("Activa el registro, abre YouTube Music desde MediaBloom y reproduce una canción. Luego cierra MediaBloom y vuelve a abrirlo: revisa la sección 'Diagnóstico desde YouTube Music' y copia el registro. Un ISRC ausente no demuestra que la canción carezca de haptics.",
                   "Start capture, open YouTube Music inside MediaBloom and play a song. Then force-close and reopen MediaBloom: check the in-guest diagnostic and copy the log. A missing ISRC does not prove the song has no haptic track."))
                .font(.caption)
                .foregroundColor(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private var mediaTagsSection: some View {
        Section(header: Text(t("Etiquetas compartidas", "Shared media labels"))) {
            Button(action: onManageMediaLabels) {
                Label(t("Organizar etiquetas de apps", "Manage app labels"),
                      systemImage: "tag")
            }
            Text(t("Música, video, ocultar o automático. Se sincronizan con BloomBox.",
                   "Music, video, hidden, or automatic. Shared with BloomBox."))
                .font(.footnote)
                .foregroundColor(.secondary)
        }
    }

    private var advancedSection: some View {
        Section {
            Button(action: onOpenAdvanced) {
                Label(t("Ajustes avanzados", "Advanced settings"), systemImage: "gearshape.2")
            }
        }
    }

    private func saveWallpaper(_ image: UIImage) {
        if MBWallpaperStore.save(image) {
            hasWallpaper = true
            enabled = true
            MBWallpaperStore.notifyChanged()
        } else { imageError = true }
    }
}


// Keep the per-app sheet separate from the large portrait/landscape view so
// Xcode can type-check each expression without an exponential ViewBuilder.
struct MBIndividualAudioSheet: View {
    let app: MediaBloomHomeApp
    @ObservedObject var model: MediaBloomHomeModel
    @Binding var audioEnabled: Bool
    let accent: Color
    let english: Bool
    let onDone: () -> Void

    private func t(_ es: String, _ en: String) -> String { english ? en : es }

    private var currentVolume: Double {
        model.apps.first(where: { $0.id == app.id })?.volume ?? app.volume
    }

    private var volumeBinding: Binding<Double> {
        Binding<Double>(get: { self.currentVolume }, set: { next in
            guard self.audioEnabled, self.app.volumeAvailable else { return }
            if let current = self.model.apps.first(where: { $0.id == self.app.id }) {
                self.model.select(current)
                self.model.setVolume(next)
            }
        })
    }

    private var guestIcon: some View {
        Group {
            if let image = app.icon {
                Image(uiImage: image).resizable().scaledToFit()
            } else {
                Image(systemName: app.symbol)
                    .font(.system(size: 28, weight: .medium))
                    .foregroundColor(.white)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .background(app.color)
            }
        }
        .frame(width: 65, height: 65)
        .clipShape(RoundedRectangle(cornerRadius: 18, style: .continuous))
    }

    private var controlSection: some View {
        VStack(spacing: 14) {
            Text(t("Volumen individual", "Individual volume"))
                .font(.title3.weight(.bold))
            Slider(value: volumeBinding, in: 0...1, step: 0.01) { editing in
                if !editing { LCUtils.appGroupUserDefault.synchronize() }
            }
            .tint(accent)
            .disabled(!app.volumeAvailable || !audioEnabled)
            Text("\(Int((currentVolume * 100).rounded()))%")
                .font(.title2.monospacedDigit())
            Text(t("El volumen general y este ajuste se combinan. No modifican el volumen de iOS.",
                   "Master and individual volume work together. iOS device volume is unchanged."))
                .multilineTextAlignment(.center)
                .font(.footnote)
                .foregroundColor(.secondary)
            if !audioEnabled {
                Button(t("Activar BloomAudio", "Enable BloomAudio")) { audioEnabled = true }
                    .buttonStyle(.borderedProminent)
            } else if !app.volumeAvailable {
                Text(t("Esta app aún no admite control individual.",
                       "This app does not support individual volume yet."))
                    .font(.footnote)
                    .foregroundColor(.secondary)
            }
        }
    }

    var body: some View {
        NavigationView {
            VStack(spacing: 16) {
                guestIcon
                Text(app.name).font(.headline)
                controlSection
                Spacer(minLength: 0)
            }
            .padding(24)
            .navigationTitle(t("Audio de app", "App audio"))
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button(t("Listo", "Done"), action: onDone)
                }
            }
        }
        .navigationViewStyle(.stack)
    }
}

// Uses EXACTLY the same App Group key and four categories as BloomBox. It can
// label a newly shared media guest even when automatic recognition lacks its ID.
// Private apps are never listed, modified, or made launchable.
struct MBMediaTagEditor: View {
    let apps: [LCAppModel]
    let english: Bool
    @Environment(\.dismiss) private var dismiss
    @State private var revision = 0

    private func t(_ es: String, _ en: String) -> String { english ? en : es }
    private var shared: [LCAppModel] {
        apps.filter { $0.uiIsShared }
            .sorted { $0.displayName.localizedStandardCompare($1.displayName) == .orderedAscending }
    }

    private func tag(for app: LCAppModel) -> String {
        _ = revision
        let key = "BloomBoxMediaTagV1." + app.bundleIdentifier.lowercased()
        return LCUtils.appGroupUserDefault.string(forKey: key) ?? "auto"
    }

    private func change(_ value: String, for app: LCAppModel) {
        guard app.uiIsShared,
              ["auto", "music", "video", "none"].contains(value) else { return }
        let shared = LCUtils.appGroupUserDefault
        let key = "BloomBoxMediaTagV1." + app.bundleIdentifier.lowercased()
        if value == "auto" { shared.removeObject(forKey: key) }
        else { shared.set(value, forKey: key) }
        shared.synchronize()
        revision += 1
        NotificationCenter.default.post(name: .mediaBloomMediaTagsDidChange, object: nil)
    }

    var body: some View {
        NavigationView {
            List {
                Section {
                    Text(t("Estas etiquetas se comparten con BloomBox. Las apps privadas no se muestran y el catálogo se actualiza al cambiar una etiqueta.",
                           "Labels sync with BloomBox. Private apps stay hidden and the catalog refreshes when a label changes."))
                        .font(.footnote)
                        .foregroundColor(.secondary)
                }
                Section(header: Text(t("Apps compartidas", "Shared apps"))) {
                    ForEach(shared, id: \.bundleIdentifier) { app in
                        Picker(app.displayName, selection: Binding(
                            get: { tag(for: app) },
                            set: { change($0, for: app) }
                        )) {
                            Text(t("Automático", "Automatic")).tag("auto")
                            Text(t("Música", "Music")).tag("music")
                            Text("Video").tag("video")
                            Text(t("Ocultar", "Hide")).tag("none")
                        }
                        .pickerStyle(.menu)
                    }
                }
            }
            .navigationTitle(t("Etiquetas multimedia", "Media labels"))
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button(t("Listo", "Done")) { dismiss() }
                }
            }
        }
        .navigationViewStyle(.stack)
        .preferredColorScheme(.light)
    }
}


// Settings-only picker: reads shared media guests without modifying containers.
private struct MBEdgePickerApp: Identifiable {
    let id: String
    let name: String
    let icon: UIImage?
}

private struct MBEdgeAppPicker: View {
    let english: Bool
    @Environment(\.dismiss) private var dismiss
    @State private var apps: [MBEdgePickerApp] = []
    @State private var selected: [String] = []
    private static let selectionKey = "MediaBloomEdgeAppIDsV1"
    // The same known multimedia IDs used by the in-player Edge menu.
    private static let preferred: [String] = [
        "com.google.ios.youtubemusic", "com.spotify.client",
        "org.videolan.vlc-ios", "com.apple.music", "com.google.ios.youtube",
        "com.amazon.mp3", "com.soundcloud.touchapp", "com.deezer.deezer",
        "com.tidal", "com.netflix.netflix", "com.amazon.aiv",
        "com.twitch.twitch", "tv.twitch", "com.plexapp.plex",
        "com.firecore.infuse", "com.disney.disneyplus"
    ]
    private func t(_ es: String, _ en: String) -> String { english ? en : es }

    private static func plist(_ url: URL) -> [String: Any] {
        guard let data = try? Data(contentsOf: url),
              let dictionary = (try? PropertyListSerialization.propertyList(
                  from: data, options: [], format: nil)) as? [String: Any]
        else { return [:] }
        return dictionary
    }

    private static func installedMedia() -> [MBEdgePickerApp] {
        guard let group = LCSharedUtils.appGroupPath() else { return [] }
        let folder = group.appendingPathComponent("LiveContainer/Applications",
                                                  isDirectory: true)
        guard let files = try? FileManager.default.contentsOfDirectory(
            at: folder, includingPropertiesForKeys: nil,
            options: [.skipsHiddenFiles]) else { return [] }
        let defaults = LCUtils.appGroupUserDefault
        let known = Set(preferred)
        var result: [MBEdgePickerApp] = []
        var seen = Set<String>()
        for file in files where file.pathExtension.lowercased() == "app" {
            let metadata = plist(file.appendingPathComponent("LCAppInfo.plist"))
            if ["isHidden", "isLocked", "LCHidden", "LCLocked"].contains(where: {
                (metadata[$0] as? NSNumber)?.boolValue == true
            }) { continue }
            let info = plist(file.appendingPathComponent("Info.plist"))
            guard let id = (info["LCBundleIdentifier"] as? String ??
                            info["CFBundleIdentifier"] as? String)?.lowercased(),
                  !id.isEmpty, seen.insert(id).inserted else { continue }
            let tag = defaults.string(forKey: "BloomBoxMediaTagV1." + id)
            if tag == "none" || (tag != "music" && tag != "video" && !known.contains(id)) {
                continue
            }
            let name = (info["CFBundleDisplayName"] as? String ??
                        info["CFBundleName"] as? String) ?? file.deletingPathExtension().lastPathComponent
            let icon = UIImage(contentsOfFile: file.appendingPathComponent("LCAppIconLight.png").path)
                ?? UIImage(contentsOfFile: file.appendingPathComponent("LCAppIconDark.png").path)
            result.append(MBEdgePickerApp(id: id, name: name, icon: icon))
        }
        return result.sorted { a, b in
            let ai = preferred.firstIndex(of: a.id) ?? preferred.count
            let bi = preferred.firstIndex(of: b.id) ?? preferred.count
            return ai == bi ? a.name.localizedStandardCompare(b.name) == .orderedAscending : ai < bi
        }
    }

    private func reload() {
        apps = Self.installedMedia()
        let valid = Set(apps.map(\.id))
        let configured = LCUtils.appGroupUserDefault.array(forKey: Self.selectionKey) as? [String]
        let defaults = configured ?? Array(apps.prefix(4).map(\.id))
        var seen = Set<String>()
        selected = Array(defaults.filter { valid.contains($0) && seen.insert($0).inserted }.prefix(4))
    }

    private func save() {
        LCUtils.appGroupUserDefault.set(selected, forKey: Self.selectionKey)
        LCUtils.appGroupUserDefault.synchronize()
    }

    @ViewBuilder
    private func appIcon(_ app: MBEdgePickerApp) -> some View {
        if let icon = app.icon {
            Image(uiImage: icon).resizable().scaledToFit()
                .frame(width: 34, height: 34)
                .clipShape(RoundedRectangle(cornerRadius: 8))
        } else {
            Image(systemName: "music.note")
                .foregroundColor(.secondary)
                .frame(width: 34, height: 34)
        }
    }

    var body: some View {
        NavigationView {
            List {
                Section(header: Text(t("En la muesca (\(selected.count)/4)",
                                       "On Edge (\(selected.count)/4)"))) {
                    if selected.isEmpty {
                        Text(t("No hay accesos seleccionados", "No selected shortcuts"))
                            .foregroundColor(.secondary)
                    }
                    ForEach(selected, id: \.self) { id in
                        if let app = apps.first(where: { $0.id == id }) {
                            HStack(spacing: 12) {
                                appIcon(app)
                                Text(app.name)
                                Spacer()
                                Button {
                                    selected.removeAll(where: { $0 == id })
                                    save()
                                } label: {
                                    Image(systemName: "minus.circle.fill")
                                        .foregroundColor(.red)
                                }
                                .buttonStyle(.borderless)
                                .accessibilityLabel(t("Quitar \(app.name)", "Remove \(app.name)"))
                            }
                        }
                    }
                    .onMove { source, destination in
                        selected.move(fromOffsets: source, toOffset: destination)
                        save()
                    }
                }
                Section(header: Text(t("Agregar aplicaciones", "Add apps"))) {
                    ForEach(apps.filter { !selected.contains($0.id) }) { app in
                        Button {
                            guard selected.count < 4 else { return }
                            selected.append(app.id)
                            save()
                        } label: {
                            HStack(spacing: 12) {
                                appIcon(app)
                                Text(app.name)
                                Spacer()
                                Image(systemName: "plus.circle")
                            }
                        }
                        .disabled(selected.count >= 4)
                    }
                    if apps.isEmpty {
                        Text(t("No hay apps multimedia compartidas. Revisa las etiquetas de las apps.",
                               "No shared media apps found. Check the app labels."))
                            .foregroundColor(.secondary)
                    }
                }
            }
            .navigationTitle(t("Apps de la muesca", "Edge apps"))
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarLeading) {
                    EditButton().disabled(selected.count < 2)
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button(t("Listo", "Done")) { dismiss() }
                }
            }
            .onAppear(perform: reload)
        }
        .navigationViewStyle(.stack)
        .preferredColorScheme(.light)
    }
}
