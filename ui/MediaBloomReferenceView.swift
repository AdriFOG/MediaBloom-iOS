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

private enum MBMediaHomeTile: Identifiable {
    case automatic(MediaBloomHomeApp)
    case manual(LCAppModel)

    var id: String {
        switch self {
        case .automatic(let app): return app.id.lowercased()
        case .manual(let app): return app.bundleIdentifier.lowercased()
        }
    }
    var name: String {
        switch self {
        case .automatic(let app): return app.name
        case .manual(let app): return app.displayName
        }
    }
}

struct MediaBloomReferenceView: View {
    @ObservedObject var model: MediaBloomHomeModel
    var allSharedGuests: [LCAppModel] = []
    @AppStorage("BloomBoxPreferredLanguage", store: LCUtils.appGroupUserDefault)
    private var preferredLanguage = "system"
    @State private var showArtistEditor = false
    @State private var showOnboarding = false
    @State private var currentRecommendation = 0
    @StateObject private var discovery = MBMusicDiscovery()
    @AppStorage("MediaBloomOnboardingCompleteV1") private var onboardingComplete = false
    @AppStorage("MediaBloomPreferredMusicAppIDV1") private var preferredPlayerID = ""
    @State private var appPage = 0
    @State private var showAppearance = false
    @State private var showMediaLabels = false
    @State private var showLibraryPicker = false
    @AppStorage("MediaBloomLauncherSelectionRevisionV1", store: LCUtils.appGroupUserDefault)
    private var launcherSelectionRevision = 0
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
    @AppStorage("MediaBloomGlassOpacityV1") private var glassOpacity = 0.35
    @AppStorage("MediaBloomGlassTintHexV1") private var glassTintHex = "F5F4F1"

    private var theme: MBReferenceTheme {
        MBReferenceTheme(accent: MBColorHex.color(accentHex),
                         background: MBColorHex.color(backgroundHex),
                         white: MBColorHex.color(cardHex),
                         gray: MBColorHex.color(mixHex))
    }

    private var glassColor: Color { MBColorHex.color(glassTintHex) }
    private var panelInk: Color {
        (glassOpacity < 0.26 || (MBColorHex.dark(glassTintHex) && glassOpacity > 0.48))
            ? Color.white : theme.ink
    }

    private var english: Bool {
        preferredLanguage == "en" || (preferredLanguage == "system" &&
            !(Locale.preferredLanguages.first ?? "es").lowercased().hasPrefix("es"))
    }
    private func t(_ es: String, _ en: String) -> String { english ? en : es }
    private var musicApps: [MediaBloomHomeApp] {
        let supported: Set<String> = ["com.google.ios.youtubemusic", "com.spotify.client",
            "com.apple.music", "com.deezer.deezer", "com.google.ios.youtube"]
        return model.apps.filter { supported.contains($0.id.lowercased()) }
    }
    private var musicApp: MediaBloomHomeApp? {
        musicApps.first(where: { $0.id == preferredPlayerID }) ??
        musicApps.first(where: { $0.id.lowercased() == "com.google.ios.youtubemusic" }) ??
        musicApps.first
    }
    private var pageCount: Int { max(1, (homeTiles.count + 5) / 6) }

    private var homeTiles: [MBMediaHomeTile] {
        _ = launcherSelectionRevision
        let automatic = model.visibleApps
        guard let chosen = LCUtils.appGroupUserDefault.stringArray(
            forKey: "MediaBloomLauncherAppIDsV1") else {
            return automatic.map { .automatic($0) }
        }
        let automaticByID = Dictionary(automatic.map { ($0.id.lowercased(), $0) },
                                       uniquingKeysWith: { first, _ in first })
        let allByID = Dictionary(allSharedGuests.map { ($0.bundleIdentifier.lowercased(), $0) },
                                uniquingKeysWith: { first, _ in first })
        let term = model.searchText.trimmingCharacters(in: .whitespacesAndNewlines)
        var seen = Set<String>()
        return chosen.compactMap { originalID -> MBMediaHomeTile? in
            let id = originalID.lowercased()
            guard seen.insert(id).inserted else { return nil }
            if let app = automaticByID[id] { return .automatic(app) }
            guard let app = allByID[id], app.uiIsShared,
                  !app.appInfo.isHidden, !app.appInfo.isLocked,
                  term.isEmpty || app.displayName.localizedCaseInsensitiveContains(term)
            else { return nil }
            return .manual(app)
        }
    }

    private func openManualApp(_ app: LCAppModel) {
        guard app.uiIsShared, !app.appInfo.isHidden, !app.appInfo.isLocked,
              let path = app.appInfo.relativeBundlePath, !path.isEmpty else { return }
        var url = URLComponents()
        url.scheme = "bloomboxmedia"
        url.host = "livecontainer-launch"
        var items = [URLQueryItem(name: "bundle-name", value: path)]
        if let folder = app.uiSelectedContainer?.folderName, !folder.isEmpty {
            items.append(URLQueryItem(name: "container-folder-name", value: folder))
        }
        url.queryItems = items
        if let destination = url.url { UIApplication.shared.open(destination) }
    }

    private func openHomeTile(_ item: MBMediaHomeTile) {
        switch item {
        case .automatic(let app):
            model.select(app)
            model.onOpenApp(app.id)
        case .manual(let app):
            openManualApp(app)
        }
    }

    private func tileVolumeText(_ item: MBMediaHomeTile) -> String {
        switch item {
        case .automatic(let app): return volumeText(for: app)
        case .manual: return t("Abrir", "Open")
        }
    }
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
        .fullScreenCover(isPresented: $showOnboarding) {
            MBMusicArtistPicker(model: model, catalog: discovery, english: english,
                                onboarding: true, musicApps: musicApps,
                                onFinish: { onboardingComplete = true; showOnboarding = false })
        }
        .sheet(isPresented: $showAppearance) {
            MediaBloomAppearanceSheet(
                onOpenAdvanced: {
                    showAppearance = false
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) {
                        model.onOpenSettings()
                    }
                },
                onManageMediaLabels: {
                    showAppearance = false
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) {
                        showMediaLabels = true
                    }
                },
                onManageAppLibrary: {
                    showAppearance = false
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.35) {
                        showLibraryPicker = true
                    }
                })
        }
        .sheet(isPresented: $showMediaLabels) {
            MBMediaTagEditor(apps: allSharedGuests, english: english)
        }
        .sheet(isPresented: $showLibraryPicker) {
            MBMediaLibraryPicker(apps: allSharedGuests, defaults: model.apps.map { $0.id },
                                 english: english)
        }
        .sheet(item: $individualApp) { app in individualVolumeEditor(app) }
        .onAppear {
            // Previous UI used the fixed 48–94% glass slider scale.
            let defaults = UserDefaults.standard
            if !defaults.bool(forKey: "MediaBloomGlassMigratedV2") {
                if let legacy = defaults.object(forKey: "MediaBloomGlassOpacityV1") as? Double {
                    glassOpacity = max(0, min(1, (legacy - 0.48) / 0.46 * 0.65))
                }
                defaults.set(true, forKey: "MediaBloomGlassMigratedV2")
            }
            wallpaperImage = MBWallpaperStore.load()
            discovery.refresh(model.favoriteArtists)
            if !onboardingComplete {
                if model.favoriteArtists.isEmpty { showOnboarding = true }
                else { onboardingComplete = true } // Preserve existing users' favorites.
            }
        }
        .onChange(of: model.favoriteArtists) { names in
            currentRecommendation = 0
            discovery.refresh(names)
        }
        .onReceive(NotificationCenter.default.publisher(for: .mediaBloomWallpaperDidChange)) { _ in
            wallpaperImage = MBWallpaperStore.load()
        }
        .onChange(of: model.searchText) { _ in appPage = 0 }
        .onChange(of: model.apps.map { $0.id }) { _ in appPage = 0 }
        .onChange(of: launcherSelectionRevision) { _ in appPage = 0 }
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
        if safeMasterVolume > 0.001 { previousMasterVolume = safeMasterVolume }
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
                .foregroundColor(panelInk)
                .minimumScaleFactor(0.8)
            if showName {
                Text(audioEnabled ? "MediaBloom" : t("Activar audio", "Enable audio"))
                    .font(.system(size: 9, weight: .regular))
                    .foregroundColor(panelInk)
                    .lineLimit(2)
                    .multilineTextAlignment(.center)
                    .minimumScaleFactor(0.7)
            }
        }
        .padding(.horizontal, 5)
        .padding(.top, 6)
        .padding(.bottom, 11)
        .frame(width: width, height: height)
        .background(MBGlassPanel(tint: glassColor, opacity: glassOpacity, cornerRadius: width / 2))
    }

    // MARK: - Search
    private func searchPill(height: CGFloat) -> some View {
        HStack(spacing: 12) {
            Image(systemName: "magnifyingglass")
                .font(.system(size: 17, weight: .medium))
                .foregroundColor(panelInk)
            TextField("", text: $model.searchText,
                      prompt: Text(t("Buscar música", "Search music"))
                        .foregroundColor(panelInk.opacity(0.68)))
                .font(.system(size: 14))
                .foregroundColor(panelInk)
                .autocorrectionDisabled()
                .submitLabel(.search)
                .onSubmit { model.onSearch(model.searchText) }
            if !model.searchText.isEmpty {
                Button { model.searchText = "" } label: {
                    Image(systemName: "xmark.circle.fill")
                        .foregroundColor(panelInk.opacity(0.75))
                }
                .buttonStyle(.plain)
            }
            Button { showLibraryPicker = true } label: {
                Image(systemName: "square.grid.2x2")
                    .font(.system(size: 15, weight: .medium))
                    .foregroundColor(panelInk.opacity(0.75))
            }
            .buttonStyle(.plain)
            .accessibilityLabel(t("Elegir apps de BloomBox", "Choose BloomBox apps"))
            Button { showAppearance = true } label: {
                Image(systemName: "gearshape")
                    .font(.system(size: 15, weight: .medium))
                    .foregroundColor(panelInk.opacity(0.75))
            }
            .buttonStyle(.plain)
            .accessibilityLabel(t("Personalizar MediaBloom", "Customize MediaBloom"))
        }
        .padding(.horizontal, 16)
        .frame(height: height)
        .background(MBGlassPanel(tint: glassColor, opacity: glassOpacity, cornerRadius: height / 2))
    }

    // MARK: - Featured mix, original circles
    // The current song now owns the WHOLE hero, in either orientation.
    // Album artwork fills the available card; landscape artist controls float
    // on an independent, legible glass surface rather than shrinking the song.
    private func featuredPanel(landscape: Bool) -> some View {
        GeometryReader { geometry in
            ZStack(alignment: .topTrailing) {
                musicButton()
                    .frame(width: geometry.size.width, height: geometry.size.height)
                if landscape {
                    artistContent(landscape: true)
                        .padding(10)
                        .frame(width: min(245, geometry.size.width * 0.45),
                               height: 105, alignment: .topLeading)
                        .background(MBGlassPanel(tint: glassColor,
                                                 opacity: glassOpacity,
                                                 cornerRadius: 20))
                        .padding(12)
                }
            }
            .frame(width: geometry.size.width, height: geometry.size.height)
            .clipShape(RoundedRectangle(cornerRadius: landscape ? 28 : 29,
                                        style: .continuous))
        }
    }

    private func musicButton() -> some View {
        MBMusicRecommendationCard(
            songs: discovery.songs, isLoading: discovery.loading,
            error: discovery.loadError,
            hasFavorites: !model.favoriteArtists.isEmpty,
            playerAvailable: musicApp != nil, english: english,
            theme: theme, currentIndex: $currentRecommendation,
            onOpenSong: { model.onPlaySong($0) },
            onSelectArtists: { showArtistEditor = true },
            onRetry: { discovery.refresh(model.favoriteArtists) })
    }

    // MARK: - Artists
    private func artistPanel(landscape: Bool) -> some View {
        artistContent(landscape: landscape)
            .padding(.horizontal, 14)
            .padding(.vertical, 13)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            .background(MBGlassPanel(tint: glassColor, opacity: glassOpacity, cornerRadius: 26))
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
            if model.favoriteArtists.isEmpty {
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
                    ForEach(Array(model.favoriteArtists.sorted().prefix(landscape ? 8 : 6)), id: \.self) { artist in
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
        .foregroundColor(panelInk)
    }

    private func artistChip(_ artist: String) -> some View {
        let active = model.favoriteArtists.contains(artist)
        return Button { model.toggleArtist(artist) } label: {
            Text(artist)
                .font(.system(size: 11, weight: .medium))
                .foregroundColor(active ? theme.white : panelInk)
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
        MBMusicArtistPicker(model: model, catalog: discovery, english: english,
                            onboarding: false, musicApps: musicApps,
                            onFinish: { showArtistEditor = false })
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
            if homeTiles.isEmpty {
                Spacer()
                Text(model.searchText.isEmpty
                    ? t("Elige apps con el botón de cuadrícula.", "Choose apps with the grid button.")
                    : t("No encontramos esa app.", "No matching app."))
                    .font(.system(size: 12))
                    .multilineTextAlignment(.center)
                Spacer()
            } else {
                TabView(selection: $appPage) {
                    ForEach(0..<pageCount, id: \.self) { page in
                        let start = page * 6
                        let end = min(start + 6, homeTiles.count)
                        let portion = Array(homeTiles[start..<end])
                        LazyVGrid(columns: Array(repeating: GridItem(.flexible(), spacing: 6), count: 3),
                                  spacing: 11) {
                            ForEach(portion) { item in homeAppCell(item) }
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
        .foregroundColor(panelInk)
        .padding(.horizontal, 10)
        .padding(.top, 13)
        .padding(.bottom, 10)
        .background(MBGlassPanel(tint: glassColor, opacity: glassOpacity, cornerRadius: 26))
    }

    @ViewBuilder
    private func homeAppCell(_ item: MBMediaHomeTile) -> some View {
        switch item {
        case .automatic(let app):
            appCell(app)
        case .manual(let guest):
            VStack(spacing: 4) {
                Button { openManualApp(guest) } label: {
                    Group {
                        if let image = guest.appInfo.iconIsDarkIcon(false) {
                            Image(uiImage: image).resizable().scaledToFill()
                        } else {
                            Image(systemName: "play.rectangle.fill")
                                .font(.system(size: 23))
                                .foregroundColor(theme.white)
                                .frame(maxWidth: .infinity, maxHeight: .infinity)
                                .background(theme.ink)
                        }
                    }
                    .frame(width: 50, height: 50)
                    .clipShape(RoundedRectangle(cornerRadius: 15, style: .continuous))
                }
                .buttonStyle(.plain)
                .accessibilityLabel(t("Abrir ", "Open ") + guest.displayName)
                Text(guest.displayName)
                    .font(.system(size: 11, weight: .medium))
                    .lineLimit(1).minimumScaleFactor(0.7)
                    .frame(maxWidth: .infinity)
                Text("MediaBloom")
                    .font(.system(size: 10))
                    .foregroundColor(panelInk)
                    .frame(height: 17)
            }
            .frame(maxWidth: .infinity)
            .contextMenu {
                Button { openManualApp(guest) } label: {
                    Label(t("Abrir con MediaBloom", "Open with MediaBloom"),
                          systemImage: "play.fill")
                }
                Button { showLibraryPicker = true } label: {
                    Label(t("Administrar apps", "Manage apps"),
                          systemImage: "square.grid.2x2")
                }
            }
        }
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
                    .foregroundColor(panelInk)
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
        let chosen = homeTiles
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
                        openHomeTile(app)
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
                            Text(tileVolumeText(app))
                                .font(.system(size: 11))
                                .lineLimit(1)
                        }
                        .foregroundColor(panelInk)
                        .padding(.horizontal, 11)
                        .frame(maxWidth: .infinity)
                        .frame(height: 69)
                        .background(MBGlassPanel(tint: glassColor,
                                                 opacity: glassOpacity, cornerRadius: 25))
                            }
                            .buttonStyle(.plain)
                            .contextMenu {
                                switch app {
                                case .automatic(let real): appVolumeContextMenu(real)
                                case .manual(let guest):
                                    Button { openManualApp(guest) } label: {
                                        Label(t("Abrir con MediaBloom", "Open with MediaBloom"),
                                              systemImage: "play.fill")
                                    }
                                }
                            }
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
        .background(MBGlassPanel(tint: glassColor, opacity: glassOpacity, cornerRadius: 27))
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
        MBIndividualAudioSheet(app: app, model: model,
                               audioEnabled: $audioEnabled,
                               accent: theme.coral, english: english,
                               onDone: { individualApp = nil })
    }

}


// MediaBloom-only launcher selection. Original LiveContainer's WebClip profile
// is reused, with its launch URL rewritten to the MediaBloom URL scheme.
private struct MBMediaLibraryPicker: View {
    let apps: [LCAppModel]
    let defaults: [String]
    let english: Bool
    @Environment(\.dismiss) private var dismiss
    @State private var selectedIDs: [String] = []
    @State private var search = ""
    @State private var showProfile = false
    @State private var profileURL = URL(string: "https://apple.com")!
    @State private var errorMessage = ""
    @AppStorage("MediaBloomLauncherSelectionRevisionV1", store: LCUtils.appGroupUserDefault)
    private var selectionRevision = 0

    private func t(_ es: String, _ en: String) -> String { english ? en : es }
    private var catalog: [LCAppModel] {
        var seen = Set<String>()
        return apps.filter {
            !$0.appInfo.isHidden && seen.insert($0.bundleIdentifier.lowercased()).inserted
        }.sorted {
            $0.displayName.localizedStandardCompare($1.displayName) == .orderedAscending
        }
    }
    private var filtered: [LCAppModel] {
        guard !search.isEmpty else { return catalog }
        return catalog.filter {
            $0.displayName.localizedCaseInsensitiveContains(search) ||
            $0.bundleIdentifier.localizedCaseInsensitiveContains(search)
        }
    }
    private func available(_ app: LCAppModel) -> Bool {
        app.uiIsShared && !app.appInfo.isLocked && !app.appInfo.isHidden
    }
    private func load() {
        let valid = Set(catalog.filter { available($0) }.map {
            $0.bundleIdentifier.lowercased()
        })
        let saved = LCUtils.appGroupUserDefault.stringArray(forKey: "MediaBloomLauncherAppIDsV1")
        let suggested = saved ?? defaults
        var seen = Set<String>()
        selectedIDs = suggested.map { $0.lowercased() }.filter {
            valid.contains($0) && seen.insert($0).inserted
        }
    }
    private func store() {
        let defaults = LCUtils.appGroupUserDefault
        defaults.set(selectedIDs, forKey: "MediaBloomLauncherAppIDsV1")
        selectionRevision += 1
        defaults.synchronize()
    }
    private func toggle(_ app: LCAppModel) {
        guard available(app) else { return }
        let id = app.bundleIdentifier.lowercased()
        if selectedIDs.contains(id) { selectedIDs.removeAll { $0 == id } }
        else { selectedIDs.append(id) }
        store()
    }

    private func installWebClip(for app: LCAppModel) {
        guard available(app),
              let path = app.appInfo.relativeBundlePath, !path.isEmpty else {
            errorMessage = t("La aplicación debe estar compartida y desbloqueada en BloomBox.",
                             "The app must be shared and unlocked in BloomBox.")
            return
        }
        var link = URLComponents()
        link.scheme = "bloomboxmedia"
        link.host = "livecontainer-launch"
        var items = [URLQueryItem(name: "bundle-name", value: path)]
        if let folder = app.uiSelectedContainer?.folderName, !folder.isEmpty {
            items.append(URLQueryItem(name: "container-folder-name", value: folder))
        }
        link.queryItems = items
        guard let destination = link.url else {
            errorMessage = t("No se pudo construir el enlace.", "Could not create the link.")
            return
        }
        // LiveContainer builds the real icon and iOS WebClip configuration;
        // replace only the host-specific fields so the profile never routes
        // through BloomBox or a generic LiveContainer installation.
        guard var profile = app.appInfo.generateWebClipConfig(
                withContainerId: app.uiSelectedContainer?.folderName,
                iconStyle: .Light) as? [String: Any],
              var payloads = profile["PayloadContent"] as? [[String: Any]],
              !payloads.isEmpty else {
            errorMessage = t("No se pudo crear el perfil.", "Could not create the profile.")
            return
        }
        let identifier = "com.kdt.BloomBoxMedia.webclip." + app.bundleIdentifier.lowercased()
        payloads[0]["URL"] = destination.absoluteString
        payloads[0]["PayloadIdentifier"] = identifier + ".icon"
        payloads[0]["PayloadUUID"] = UUID().uuidString
        payloads[0]["PayloadOrganization"] = "MediaBloom"
        payloads[0]["PayloadDescription"] = t(
            "Abre \(app.displayName) exclusivamente mediante MediaBloom.",
            "Launch \(app.displayName) through MediaBloom.")
        profile["PayloadContent"] = payloads
        profile["PayloadIdentifier"] = identifier
        profile["PayloadUUID"] = UUID().uuidString
        profile["PayloadOrganization"] = "MediaBloom"
        profile["PayloadDescription"] = t(
            "Acceso al inicio para \(app.displayName) mediante MediaBloom.",
            "Home Screen shortcut for \(app.displayName) through MediaBloom.")
        profile["ConsentText"] = ["default": t(
            "Instala un icono que abre \(app.displayName) usando MediaBloom.",
            "Install a Home Screen icon that opens \(app.displayName) using MediaBloom.")]
        do {
            let data = try PropertyListSerialization.data(
                fromPropertyList: profile, format: .xml, options: 0)
            guard let url = URL(string: "data:application/x-apple-aspen-config;base64,\(data.base64EncodedString())") else {
                throw CocoaError(.fileWriteInvalidFileName)
            }
            profileURL = url
            showProfile = true
        } catch {
            errorMessage = error.localizedDescription
        }
    }

    @ViewBuilder
    private func icon(for app: LCAppModel) -> some View {
        if let image = app.appInfo.iconIsDarkIcon(false) {
            Image(uiImage: image).resizable().scaledToFill()
                .frame(width: 40, height: 40)
                .clipShape(RoundedRectangle(cornerRadius: 11, style: .continuous))
        } else {
            Image(systemName: "app.fill")
                .frame(width: 40, height: 40)
                .foregroundColor(.secondary)
        }
    }

    var body: some View {
        NavigationView {
            List {
                Section {
                    Text(t("Elige las apps que aparecerán en MediaBloom. Las privadas o bloqueadas se muestran, pero no se pueden abrir desde otra app.",
                           "Choose apps for MediaBloom. Private or locked apps are shown but cannot run from another host."))
                        .font(.footnote).foregroundColor(.secondary)
                    Text(t("Seleccionadas: \(selectedIDs.count)", "Selected: \(selectedIDs.count)"))
                        .font(.subheadline.weight(.semibold))
                }
                Section(header: Text(t("Aplicaciones de BloomBox", "BloomBox apps"))) {
                    ForEach(filtered, id: \.bundleIdentifier) { app in
                        let enabled = available(app)
                        let selected = selectedIDs.contains(app.bundleIdentifier.lowercased())
                        HStack(spacing: 12) {
                            Button { toggle(app) } label: {
                                HStack(spacing: 12) {
                                    icon(for: app)
                                    VStack(alignment: .leading, spacing: 3) {
                                        Text(app.displayName)
                                            .foregroundColor(.primary).lineLimit(1)
                                        if !enabled {
                                            Text(app.appInfo.isLocked
                                                 ? t("Bloqueada", "Locked")
                                                 : t("Privada: compártela desde BloomBox",
                                                     "Private: share it in BloomBox"))
                                                .font(.caption2).foregroundColor(.secondary)
                                        }
                                    }
                                    Spacer(minLength: 0)
                                    Image(systemName: selected ? "checkmark.circle.fill" : "circle")
                                        .foregroundColor(selected ? .accentColor : .secondary)
                                }
                            }
                            .buttonStyle(.plain)
                            .disabled(!enabled)
                            if enabled {
                                Button { installWebClip(for: app) } label: {
                                    Image(systemName: "square.and.arrow.up.on.square")
                                        .font(.system(size: 19))
                                }
                                .buttonStyle(.borderless)
                                .accessibilityLabel(t("Crear acceso a \(app.displayName)",
                                                      "Create shortcut to \(app.displayName)"))
                            }
                        }
                        .padding(.vertical, 3)
                    }
                    if filtered.isEmpty {
                        Text(t("No encontramos aplicaciones.", "No apps found."))
                            .foregroundColor(.secondary)
                    }
                }
                Section {
                    Text(t("Para colocar un icono en el inicio, toca el botón de acceso directo e instala el perfil desde Ajustes de iOS. El enlace abre MediaBloom, no BloomBox.",
                           "Tap the shortcut icon and install the profile in iOS Settings. The shortcut targets MediaBloom, not BloomBox."))
                        .font(.footnote).foregroundColor(.secondary)
                }
            }
            .searchable(text: $search, prompt: t("Buscar apps", "Search apps"))
            .navigationTitle(t("Apps de BloomBox", "BloomBox apps"))
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button(t("Listo", "Done")) { dismiss() }
                }
            }
            .onAppear(perform: load)
            .fullScreenCover(isPresented: $showProfile) {
                SafariView(url: $profileURL)
            }
            .alert(t("No se pudo crear el acceso", "Could not create shortcut"),
                   isPresented: Binding(
                    get: { !errorMessage.isEmpty },
                    set: { if !$0 { errorMessage = "" } })) {
                Button("OK", role: .cancel) { errorMessage = "" }
            } message: {
                Text(errorMessage)
            }
        }
        .navigationViewStyle(.stack)
        .preferredColorScheme(.light)
    }
}
