import SwiftUI
import UIKit

// MediaBloom visual reference: Modo música (vertical) / Modo música (horizontal).
// This is a presentation-only replacement for MediaBloomHomeView.
// Keep all guest launches and BloomAudio writes in MediaBloomHomeBridge.
private enum MBReferenceStyle {
    static let background = Color(red: 0.055, green: 0.058, blue: 0.064)
    static let white = Color(red: 0.961, green: 0.957, blue: 0.945)
    static let gray = Color(red: 0.794, green: 0.804, blue: 0.788)
    static let ink = Color(red: 0.097, green: 0.101, blue: 0.113)
    static let coral = Color(red: 1.0, green: 0.535, blue: 0.415)
    static let track = Color(red: 0.874, green: 0.870, blue: 0.860)
    static let pill = Color(red: 0.890, green: 0.891, blue: 0.879)
}

struct MediaBloomReferenceView: View {
    @ObservedObject var model: MediaBloomHomeModel
    @AppStorage("BloomBoxPreferredLanguage", store: LCUtils.appGroupUserDefault)
    private var preferredLanguage = "system"
    @State private var showArtistEditor = false
    @State private var newArtist = ""
    @State private var appPage = 0
    @State private var showAppearance = false
    @State private var individualApp: MediaBloomHomeApp?
    @State private var wallpaperImage: UIImage?
    @State private var previousMasterVolume = 0.85

    // This is MediaBloom's GENERAL gain. Guest gains remain independent.
    @AppStorage("MediaBloomMasterGainV1", store: LCUtils.appGroupUserDefault)
    private var masterVolume = 1.0
    @AppStorage("MediaBloomAudioProbeEnabledV1", store: LCUtils.appGroupUserDefault)
    private var audioEnabled = false
    @AppStorage("MediaBloomAccentHexV1") private var accentHex = "FF896A"
    @AppStorage("MediaBloomBackgroundHexV1") private var backgroundHex = "0E0F11"
    @AppStorage("MediaBloomCardHexV1") private var cardHex = "F5F4F1"
    @AppStorage("MediaBloomMixCardHexV1") private var mixHex = "CBCDC9"
    @AppStorage("MediaBloomWallpaperEnabledV1") private var wallpaperEnabled = true
    @AppStorage("MediaBloomWallpaperBlurV1") private var wallpaperBlur = 14.0

    private var theme: MBReferenceTheme {
        MBReferenceTheme(accent: MBColorHex.color(accentHex),
                         background: MBColorHex.color(backgroundHex),
                         white: MBColorHex.color(cardHex),
                         gray: MBColorHex.color(mixHex))
    }

    private var english: Bool {
        preferredLanguage == "en" || (preferredLanguage == "system" &&
            !(Locale.preferredLanguages.first ?? "es").lowercased().hasPrefix("es"))
    }
    private func t(_ es: String, _ en: String) -> String { english ? en : es }
    private var musicApp: MediaBloomHomeApp? {
        model.apps.first(where: { $0.id.lowercased() == "com.google.ios.youtubemusic" }) ??
        model.apps.first(where: { $0.symbol == "music.note" })
    }
    private var pageCount: Int { max(1, (model.visibleApps.count + 5) / 6) }
    private var artistSummary: String {
        let count = model.favoriteArtists.count
        if english { return count == 1 ? "your 1 artist" : "your \(count) artists" }
        return count == 1 ? "tu artista favorito" : "tus \(count) artistas"
    }

    var body: some View {
        GeometryReader { geometry in
            Group {
                if geometry.size.width > geometry.size.height {
                    landscapeLayout(width: geometry.size.width, height: geometry.size.height)
                } else {
                    portraitLayout(width: geometry.size.width)
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .background(wallpaperBackground)
        .preferredColorScheme(.dark)
        .sheet(isPresented: $showArtistEditor) { artistEditor }
        .sheet(isPresented: $showAppearance) {
            MediaBloomAppearanceSheet(onOpenAdvanced: {
                showAppearance = false
                // Avoid opening a second sheet while the first animates.
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) {
                    model.onOpenSettings()
                }
            })
        }
        .sheet(item: $individualApp) { app in individualVolumeEditor(app) }
        .onAppear { wallpaperImage = MBWallpaperStore.load() }
        .onReceive(NotificationCenter.default.publisher(for: .mediaBloomWallpaperDidChange)) { _ in
            wallpaperImage = MBWallpaperStore.load()
        }
        .onChange(of: model.searchText) { _ in appPage = 0 }
        .onChange(of: model.apps.map { $0.id }) { _ in appPage = 0 }
    }

    private func portraitLayout(width: CGFloat) -> some View {
        let contentWidth = max(200, width - 24 - 56 - 8)
        // Move the bottom app card slightly up to clear the Home indicator.
        let heroHeight = contentWidth * (668.0 / 604.0) - 8
        let artistsHeight = max(112, contentWidth * (248.0 / 604.0) - 6)
        let appsHeight = max(228, contentWidth * (508.0 / 604.0) - 20)
        let totalHeight = 40 + heroHeight + artistsHeight + appsHeight + 24
        return ScrollView(.vertical, showsIndicators: false) {
            HStack(alignment: .top, spacing: 8) {
                volumeRail(height: totalHeight, width: 56, showName: true)
                VStack(spacing: 8) {
                    searchPill(height: 40)
                    featuredPanel(landscape: false)
                        .frame(height: heroHeight)
                    artistPanel(landscape: false)
                        .frame(height: artistsHeight)
                    appGridPanel()
                        .frame(height: appsHeight)
                }
                .frame(width: contentWidth)
            }
            .frame(maxWidth: .infinity)
            .padding(.horizontal, 12)
            .padding(.top, 6)
            .padding(.bottom, 38)
        }
    }

    private func landscapeLayout(width: CGFloat, height: CGFloat) -> some View {
        let rightWidth = min(198, max(166, width * 0.24))
        let usableHeight = max(280, height - 14)
        return HStack(spacing: 8) {
            volumeRail(height: usableHeight, width: 54, showName: false)
            featuredPanel(landscape: true)
                .frame(maxWidth: .infinity)
                .frame(height: usableHeight)
            VStack(spacing: 8) {
                searchPill(height: 38)
                landscapeAppPanel()
            }
            .frame(width: rightWidth, height: usableHeight)
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 7)
    }

    // MARK: - General MediaBloom volume (not iPhone's system volume)
    private var safeMasterVolume: Double { max(0, min(1, masterVolume.isFinite ? masterVolume : 1)) }

    private func setMasterVolume(_ value: Double) {
        guard audioEnabled else { return }
        let safe = value.isFinite ? max(0, min(1, value)) : 1
        if safe > 0.001 { previousMasterVolume = safe }
        masterVolume = safe
    }

    private func toggleMasterMute() {
        if !audioEnabled {
            audioEnabled = true
            return // Reopen a guest for new audio hooks to become active.
        }
        let next = safeMasterVolume < 0.001 ? max(0.05, previousMasterVolume) : 0
        setMasterVolume(next)
        LCUtils.appGroupUserDefault.synchronize()
        MediaBloomControlCenterSync.notify()
    }

    private func volumeRail(height: CGFloat, width: CGFloat, showName: Bool) -> some View {
        let level = safeMasterVolume
        return VStack(spacing: 7) {
            Button(action: toggleMasterMute) {
                Image(systemName: !audioEnabled ? "speaker.slash.fill" :
                      level < 0.001 ? "speaker.slash.fill" : "speaker.wave.2.fill")
                    .font(.system(size: 19, weight: .medium))
                    .foregroundColor(theme.white)
                    .frame(width: width - 10, height: width - 10)
                    .background(theme.ink, in: Circle())
            }
            .buttonStyle(.plain)
            .accessibilityLabel(!audioEnabled ? t("Activar BloomAudio", "Enable BloomAudio") :
                level < 0.001 ? t("Quitar silencio general", "Unmute MediaBloom") :
                t("Silenciar MediaBloom", "Mute MediaBloom"))

            GeometryReader { track in
                ZStack(alignment: .bottom) {
                    Capsule().fill(theme.track)
                    Capsule().fill(theme.coral)
                        .frame(height: track.size.height * level)
                }
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { drag in
                        guard audioEnabled, track.size.height > 0 else { return }
                        setMasterVolume(1 - Double(drag.location.y / track.size.height))
                    }
                    .onEnded { _ in
                        if audioEnabled {
                            LCUtils.appGroupUserDefault.synchronize()
                            MediaBloomControlCenterSync.notify()
                        }
                    })
            }
            .frame(width: 30)
            .opacity(audioEnabled ? 1.0 : 0.5)
            .accessibilityElement()
            .accessibilityLabel(t("Volumen general de MediaBloom", "MediaBloom master volume"))
            .accessibilityValue("\(Int((level * 100).rounded()))%")
            .accessibilityAdjustableAction { direction in
                guard audioEnabled else { return }
                setMasterVolume(level + (direction == .increment ? 0.05 : -0.05))
                LCUtils.appGroupUserDefault.synchronize()
                MediaBloomControlCenterSync.notify()
            }

            Text("\(Int((level * 100).rounded()))%")
                .font(.system(size: 14, weight: .bold))
                .foregroundColor(theme.ink)
                .minimumScaleFactor(0.8)
            if showName {
                Text(audioEnabled ? "MediaBloom" : t("Activar audio", "Enable audio"))
                    .font(.system(size: 9, weight: .regular))
                    .foregroundColor(theme.ink)
                    .lineLimit(2)
                    .multilineTextAlignment(.center)
                    .minimumScaleFactor(0.7)
            }
        }
        .padding(.horizontal, 5)
        .padding(.top, 6)
        .padding(.bottom, 11)
        .frame(width: width, height: height)
        .background(theme.white, in: RoundedRectangle(cornerRadius: width / 2, style: .continuous))
    }

    // MARK: - Search
    private func searchPill(height: CGFloat) -> some View {
        HStack(spacing: 12) {
            Image(systemName: "magnifyingglass")
                .font(.system(size: 17, weight: .medium))
                .foregroundColor(theme.ink)
            TextField("", text: $model.searchText,
                      prompt: Text(t("Buscar música", "Search music"))
                        .foregroundColor(theme.ink.opacity(0.38)))
                .font(.system(size: 14))
                .foregroundColor(theme.ink)
                .autocorrectionDisabled()
                .submitLabel(.search)
                .onSubmit { model.onSearch(model.searchText) }
            if !model.searchText.isEmpty {
                Button { model.searchText = "" } label: {
                    Image(systemName: "xmark.circle.fill")
                        .foregroundColor(theme.ink.opacity(0.5))
                }
                .buttonStyle(.plain)
            }
            Button { showAppearance = true } label: {
                Image(systemName: "gearshape")
                    .font(.system(size: 15, weight: .medium))
                    .foregroundColor(theme.ink.opacity(0.65))
            }
            .buttonStyle(.plain)
            .accessibilityLabel(t("Personalizar MediaBloom", "Customize MediaBloom"))
        }
        .padding(.horizontal, 16)
        .frame(height: height)
        .background(theme.white, in: Capsule())
    }

    // MARK: - Featured mix, original circles
    private func featuredPanel(landscape: Bool) -> some View {
        GeometryReader { size in
            ZStack(alignment: .topLeading) {
                RoundedRectangle(cornerRadius: landscape ? 28 : 29, style: .continuous)
                    .fill(theme.gray)
                GeometryReader { g in
                    if landscape {
                        Circle().fill(theme.ink)
                            .frame(width: g.size.height * 0.79, height: g.size.height * 0.79)
                            .position(x: g.size.width * 0.62, y: g.size.height * 0.34)
                        Circle().fill(theme.coral)
                            .frame(width: g.size.height * 0.32, height: g.size.height * 0.32)
                            .position(x: g.size.width * 0.71, y: g.size.height * 0.37)
                        Circle().fill(theme.white)
                            .frame(width: g.size.height * 1.13, height: g.size.height * 1.13)
                            .position(x: g.size.width * 0.70, y: g.size.height * 1.10)
                    } else {
                        Circle().fill(theme.ink)
                            .frame(width: g.size.width * 0.50, height: g.size.width * 0.50)
                            .position(x: g.size.width * 0.69, y: g.size.height * 0.31)
                        Circle().fill(theme.coral)
                            .frame(width: g.size.width * 0.215, height: g.size.width * 0.215)
                            .position(x: g.size.width * 0.83, y: g.size.height * 0.37)
                        Circle().fill(theme.white)
                            .frame(width: g.size.width * 0.99, height: g.size.width * 0.99)
                            .position(x: g.size.width * 0.83, y: g.size.height * 0.90)
                    }
                }
                .clipShape(RoundedRectangle(cornerRadius: 29, style: .continuous))

                VStack(alignment: .leading, spacing: 3) {
                    Text(t("Mix de tus favoritos", "Your favorites mix"))
                        .font(.system(size: landscape ? 12 : 12, weight: .medium))
                    Text(t("Basado en", "Based on") + "\n" + artistSummary)
                        .font(.system(size: landscape ? 20 : 19, weight: .bold))
                        .lineSpacing(0)
                        .fixedSize(horizontal: false, vertical: true)
                }
                .foregroundColor(theme.ink)
                .padding(.leading, landscape ? 17 : 16)
                .padding(.top, landscape ? 16 : 16)

                if landscape {
                    HStack(alignment: .bottom, spacing: 10) {
                        musicButton()
                            .fixedSize(horizontal: true, vertical: false)
                        Spacer(minLength: 0)
                        artistContent(landscape: true)
                            .frame(width: max(225, size.size.width * 0.54),
                                   height: min(112, size.size.height * 0.45), alignment: .bottom)
                    }
                    .padding(.leading, 14)
                    .padding(.trailing, 14)
                    .padding(.bottom, 12)
                    .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottom)
                } else {
                    VStack {
                        Spacer()
                        HStack {
                            musicButton()
                            Spacer(minLength: 0)
                        }
                        .padding(.bottom, 12)
                        .padding(.leading, 12)
                    }
                }
            }
        }
    }

    private func musicButton() -> some View {
        Button(action: model.onPlayRecommendation) {
            HStack(spacing: 8) {
                Image(systemName: "play.fill")
                    .font(.system(size: 14))
                    .foregroundColor(theme.white)
                    .frame(width: 35, height: 35)
                    .background(theme.ink, in: Circle())
                Text(musicApp.map { t("Abrir en \($0.name)", "Open in \($0.name)") } ??
                     t("Agrega una app de música", "Add a music app"))
                    .font(.system(size: 12, weight: .medium))
                    .foregroundColor(theme.ink)
                    .lineLimit(1)
                    .minimumScaleFactor(0.8)
            }
            .padding(.leading, 5)
            .padding(.trailing, 16)
            .frame(height: 45)
            .background(theme.white, in: Capsule())
        }
        .buttonStyle(.plain)
        .disabled(musicApp == nil)
    }

    // MARK: - Artists
    private func artistPanel(landscape: Bool) -> some View {
        artistContent(landscape: landscape)
            .padding(.horizontal, 14)
            .padding(.vertical, 13)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            .background(theme.white, in: RoundedRectangle(cornerRadius: 26, style: .continuous))
    }

    private func artistContent(landscape: Bool) -> some View {
        VStack(alignment: .leading, spacing: landscape ? 8 : 8) {
            HStack(spacing: 6) {
                Text(t("ELIGE TUS ARTISTAS", "PICK YOUR ARTISTS"))
                    .font(.system(size: landscape ? 12 : 12, weight: .semibold))
                    .tracking(0.4)
                    .lineLimit(1)
                    .minimumScaleFactor(0.75)
                Spacer(minLength: 0)
                Button { showArtistEditor = true } label: {
                    Image(systemName: "plus.circle.fill")
                        .font(.system(size: 15))
                }
                .buttonStyle(.plain)
                .accessibilityLabel(t("Agregar artista", "Add artist"))
            }
            if model.artists.isEmpty {
                Button { showArtistEditor = true } label: {
                    Text(t("+ Agregar artista", "+ Add an artist"))
                        .font(.system(size: 12, weight: .medium))
                        .padding(.horizontal, 12)
                        .padding(.vertical, 8)
                        .overlay(Capsule().stroke(theme.ink, lineWidth: 1))
                }
                .buttonStyle(.plain)
            } else {
                LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 5),
                                         count: landscape ? 4 : 3), spacing: 7) {
                    ForEach(Array(model.artists.prefix(landscape ? 8 : 6)), id: \.self) { artist in
                        artistChip(artist)
                    }
                }
            }
            Spacer(minLength: 0)
            HStack(spacing: 4) {
                Capsule().fill(theme.ink).frame(width: 22, height: 2)
                Capsule().fill(theme.track).frame(width: 22, height: 2)
                Capsule().fill(theme.track).frame(width: 22, height: 2)
            }
        }
        .foregroundColor(theme.ink)
    }

    private func artistChip(_ artist: String) -> some View {
        let active = model.favoriteArtists.contains(artist)
        return Button { model.toggleArtist(artist) } label: {
            Text(artist)
                .font(.system(size: 11, weight: .medium))
                .foregroundColor(active ? theme.white : theme.ink)
                .lineLimit(1)
                .minimumScaleFactor(0.65)
                .frame(maxWidth: .infinity)
                .frame(height: 25)
                .background(active ? theme.ink : theme.white, in: Capsule())
                .overlay(Capsule().stroke(theme.ink, lineWidth: 1))
        }
        .buttonStyle(.plain)
        .accessibilityAddTraits(active ? .isSelected : [])
        .contextMenu {
            Button(role: .destructive) { model.removeArtist(artist) } label: {
                Label(t("Eliminar artista", "Remove artist"), systemImage: "trash")
            }
        }
    }

    private var artistEditor: some View {
        NavigationView {
            VStack(alignment: .leading, spacing: 14) {
                TextField(t("Nombre del artista", "Artist name"), text: $newArtist)
                    .textInputAutocapitalization(.words)
                    .textFieldStyle(.roundedBorder)
                    .submitLabel(.done)
                    .onSubmit(addArtist)
                Text(t("Se guarda únicamente en este iPhone. Mantén presionado un artista para eliminarlo.",
                       "Saved on this iPhone only. Long-press an artist to remove it."))
                    .font(.footnote).foregroundColor(.secondary)
                Spacer()
            }
            .padding(20)
            .navigationTitle(t("Agregar artista", "Add artist"))
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button(t("Cancelar", "Cancel")) { showArtistEditor = false }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button(t("Guardar", "Save"), action: addArtist)
                        .disabled(newArtist.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                }
            }
        }
        .navigationViewStyle(.stack)
    }

    private func addArtist() {
        model.addArtist(newArtist)
        newArtist = ""
        showArtistEditor = false
    }

    // MARK: - Six real installed apps, 3x2, with actual selection and paging
    private func appGridPanel() -> some View {
        VStack(spacing: 5) {
            HStack {
                Text(t("TUS APPS", "YOUR APPS"))
                    .font(.system(size: 12, weight: .semibold))
                    .tracking(0.3)
                Spacer()
                Text("\(min(appPage + 1, pageCount)) / \(pageCount)")
                    .font(.system(size: 11))
            }
            .padding(.horizontal, 6)
            if model.visibleApps.isEmpty {
                Spacer()
                Text(model.apps.isEmpty
                    ? t("Comparte apps de música o video desde BloomBox.", "Share music or video apps from BloomBox.")
                    : t("No encontramos esa app.", "No matching app."))
                    .font(.system(size: 12))
                    .multilineTextAlignment(.center)
                Spacer()
            } else {
                TabView(selection: $appPage) {
                    ForEach(0..<pageCount, id: \.self) { page in
                        let start = page * 6
                        let end = min(start + 6, model.visibleApps.count)
                        let portion = Array(model.visibleApps[start..<end])
                        LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 6), count: 3),
                                  spacing: 11) {
                            ForEach(portion) { app in appCell(app) }
                        }
                        .frame(maxHeight: .infinity, alignment: .center)
                        .padding(.horizontal, 3)
                        .tag(page)
                    }
                }
                .tabViewStyle(.page(indexDisplayMode: .never))
                .frame(maxHeight: .infinity)
                HStack(spacing: 6) {
                    ForEach(0..<pageCount, id: \.self) { index in
                        Capsule().fill(index == appPage ? theme.ink : theme.track)
                            .frame(width: index == appPage ? 16 : 6, height: 6)
                    }
                }
                .padding(.bottom, 1)
            }
        }
        .foregroundColor(theme.ink)
        .padding(.horizontal, 10)
        .padding(.top, 13)
        .padding(.bottom, 10)
        .background(theme.white, in: RoundedRectangle(cornerRadius: 26, style: .continuous))
    }

    private func appCell(_ app: MediaBloomHomeApp) -> some View {
        let selected = model.selectedID == app.id
        return VStack(spacing: 4) {
            Button {
                model.select(app)
                model.onOpenApp(app.id)
            } label: {
                appIcon(app, size: 50)
                    .overlay(RoundedRectangle(cornerRadius: 17, style: .continuous)
                        .stroke(selected ? theme.coral : Color.clear, lineWidth: 2)
                        .padding(-3))
            }
            .buttonStyle(.plain)
            .accessibilityLabel(t("Abrir ", "Open ") + app.name)
            Text(app.name)
                .font(.system(size: 11, weight: .medium))
                .lineLimit(1)
                .minimumScaleFactor(0.7)
                .frame(maxWidth: .infinity)
            Button { individualApp = app } label: {
                Text(volumeText(for: app))
                    .font(.system(size: 10))
                    .foregroundColor(theme.ink)
                    .padding(.horizontal, 7)
                    .frame(height: 17)
                    .background(theme.pill, in: Capsule())
            }
            .buttonStyle(.plain)
            .accessibilityLabel(t("Volumen individual de ", "Individual volume for ") + app.name)
        }
        .frame(maxWidth: .infinity)
        .contextMenu { appVolumeContextMenu(app) }
    }

    private func appIcon(_ app: MediaBloomHomeApp, size: CGFloat) -> some View {
        Group {
            if let real = app.icon {
                Image(uiImage: real)
                    .resizable()
                    .aspectRatio(contentMode: .fill)
            } else {
                Image(systemName: app.symbol)
                    .font(.system(size: size * 0.44, weight: .medium))
                    .foregroundColor(.white)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .background(app.color)
            }
        }
        .frame(width: size, height: size)
        .clipShape(RoundedRectangle(cornerRadius: size * 0.29, style: .continuous))
    }

    private func volumeText(for app: MediaBloomHomeApp) -> String {
        guard app.volumeAvailable else { return t("No disp.", "N/A") }
        return "\(Int((app.volume * 100).rounded()))%"
    }

    // MARK: - Landscape's independent right-hand app list
    private func landscapeAppPanel() -> some View {
        let chosen = model.visibleApps
        return VStack(spacing: 0) {
            if chosen.isEmpty {
                Text(t("No hay apps multimedia", "No media apps"))
                    .font(.system(size: 12))
                    .padding(16)
                Spacer()
            } else {
                ScrollView(.vertical, showsIndicators: false) {
                    VStack(spacing: 0) {
                        ForEach(chosen.indices, id: \.self) { index in
                            let app = chosen[index]
                            Button {
                        model.select(app)
                        model.onOpenApp(app.id)
                    } label: {
                        HStack(spacing: 8) {
                            Text(String(format: "%02d", index + 1))
                                .font(.system(size: 12, weight: .semibold))
                                .frame(width: 28, height: 21)
                                .overlay(Capsule().stroke(theme.ink, lineWidth: 1))
                            Text(app.name)
                                .font(.system(size: 12, weight: .semibold))
                                .lineLimit(1)
                                .minimumScaleFactor(0.7)
                            Spacer(minLength: 0)
                            Text(volumeText(for: app))
                                .font(.system(size: 11))
                                .lineLimit(1)
                        }
                        .foregroundColor(theme.ink)
                        .padding(.horizontal, 11)
                        .frame(maxWidth: .infinity)
                        .frame(height: 69)
                        .background(index == chosen.count - 1 ? theme.white : theme.gray,
                                    in: RoundedRectangle(cornerRadius: 25, style: .continuous))
                            }
                            .buttonStyle(.plain)
                            .contextMenu { appVolumeContextMenu(app) }
                        }
                    }
                }
            }
            HStack(spacing: 8) {
                if let selected = model.selected {
                    Button { model.onOpenApp(selected.id) } label: {
                        appIcon(selected, size: 43)
                    }
                    .buttonStyle(.plain)
                }
                Text(t("Favorita", "Favorite"))
                    .font(.system(size: 11))
                    .foregroundColor(theme.white)
                    .padding(.horizontal, 10)
                    .padding(.vertical, 4)
                    .background(theme.ink, in: Capsule())
                Spacer(minLength: 0)
            }
            .padding(.horizontal, 10)
            .padding(.bottom, 10)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(theme.white, in: RoundedRectangle(cornerRadius: 27, style: .continuous))
    }
    // MARK: - Custom wallpaper and per-guest volume
    private var wallpaperBackground: some View {
        ZStack {
            theme.background
            if wallpaperEnabled, let wallpaper = wallpaperImage {
                GeometryReader { frame in
                    Image(uiImage: wallpaper)
                        .resizable()
                        .scaledToFill()
                        .frame(width: frame.size.width, height: frame.size.height)
                        .clipped()
                        .blur(radius: CGFloat(max(0, min(40, wallpaperBlur))), opaque: true)
                        .overlay(Color.black.opacity(0.10))
                }
            }
        }
        .ignoresSafeArea()
    }

    @ViewBuilder
    private func appVolumeContextMenu(_ app: MediaBloomHomeApp) -> some View {
        Button { individualApp = app } label: {
            Label(t("Volumen individual", "Individual volume"), systemImage: "slider.horizontal.3")
        }
        Button { model.onOpenApp(app.id) } label: {
            Label(t("Abrir aplicación", "Open app"), systemImage: "play.fill")
        }
    }

    private func individualVolumeEditor(_ app: MediaBloomHomeApp) -> some View {
        let liveVolume = model.apps.first(where: { $0.id == app.id })?.volume ?? app.volume
        return NavigationView {
            VStack(spacing: 18) {
                appIcon(app, size: 66)
                Text(app.name)
                    .font(.headline)
                Text(t("Volumen individual", "Individual volume"))
                    .font(.title3.weight(.bold))
                Slider(value: Binding(
                    get: { model.apps.first(where: { $0.id == app.id })?.volume ?? liveVolume },
                    set: { value in
                        guard app.volumeAvailable, audioEnabled else { return }
                        if let current = model.apps.first(where: { $0.id == app.id }) {
                            model.select(current)
                            model.setVolume(value)
                        }
                    }), in: 0...1, step: 0.01) { editing in
                        if !editing {
                            LCUtils.appGroupUserDefault.synchronize()
                            model.onVolumeCommit()
                        }
                    }
                    .tint(theme.coral)
                    .disabled(!app.volumeAvailable || !audioEnabled)
                Text("\(Int((model.apps.first(where: { $0.id == app.id })?.volume ?? liveVolume) * 100).rounded())%")
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
                    Text(t("Esta app aún no es compatible con el control individual.",
                           "Individual gain is not supported by this app yet."))
                        .font(.footnote).foregroundColor(.secondary)
                }
                Spacer(minLength: 0)
            }
            .padding(24)
            .navigationTitle(t("Audio de app", "App audio"))
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button(t("Listo", "Done")) { individualApp = nil }
                }
            }
        }
        .navigationViewStyle(.stack)
    }

}