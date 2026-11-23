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
    @State private var showPhotos = false
    @State private var showFiles = false
    @State private var hasWallpaper = false
    @State private var imageError = false

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
                advancedSection
            }
            .navigationTitle(t("Apariencia", "Appearance"))
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button(t("Listo", "Done")) { dismiss() }
                }
            }
            .onAppear { hasWallpaper = MBWallpaperStore.load() != nil }
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

    private var audioSection: some View {
        Section(header: Text("BloomAudio")) {
            Toggle(t("Activar control de audio", "Enable audio control"), isOn: $audioEnabled)
            Text(t("Después de activarlo, vuelve a abrir las apps multimedia. El volumen general se combina con el volumen individual.",
                   "Reopen media apps after enabling. Master and individual gain combine."))
                .font(.footnote).foregroundColor(.secondary)
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