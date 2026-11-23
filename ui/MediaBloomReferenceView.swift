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
        .background(MBReferenceStyle.background.ignoresSafeArea())
        .preferredColorScheme(.dark)
        .sheet(isPresented: $showArtistEditor) { artistEditor }
        .onChange(of: model.searchText) { _ in appPage = 0 }
        .onChange(of: model.apps.map { $0.id }) { _ in appPage = 0 }
    }

    private func portraitLayout(width: CGFloat) -> some View {
        let contentWidth = max(200, width - 24 - 56 - 8)
        let heroHeight = contentWidth * (668.0 / 604.0)
        let artistsHeight = max(122, contentWidth * (248.0 / 604.0))
        let appsHeight = max(252, contentWidth * (508.0 / 604.0))
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
            .padding(.top, 10)
            .padding(.bottom, 26)
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

    // MARK: - Shared volume rail
    private func volumeRail(height: CGFloat, width: CGFloat, showName: Bool) -> some View {
        let current = model.selected
        let available = current?.volumeAvailable ?? false
        let level = max(0, min(1, model.muted ? 0 : (current?.volume ?? 0)))
        return VStack(spacing: 7) {
            Button(action: model.toggleMute) {
                Image(systemName: model.muted ? "speaker.slash.fill" : "speaker.wave.2.fill")
                    .font(.system(size: 19, weight: .medium))
                    .foregroundColor(MBReferenceStyle.white)
                    .frame(width: width - 10, height: width - 10)
                    .background(MBReferenceStyle.ink, in: Circle())
            }
            .buttonStyle(.plain)
            .disabled(!available)
            .accessibilityLabel(model.muted ? t("Quitar silencio", "Unmute") : t("Silenciar", "Mute"))

            GeometryReader { track in
                ZStack(alignment: .bottom) {
                    Capsule().fill(MBReferenceStyle.track)
                    Capsule().fill(MBReferenceStyle.coral)
                        .frame(height: track.size.height * level)
                }
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { drag in
                        guard available, track.size.height > 0 else { return }
                        model.setVolume(1 - Double(drag.location.y / track.size.height))
                    }
                    .onEnded { _ in
                        if available { model.onVolumeCommit() }
                    })
            }
            .frame(width: 30)
            .accessibilityElement()
            .accessibilityLabel(t("Volumen de ", "Volume for ") + (current?.name ?? "—"))
            .accessibilityValue("\(Int((level * 100).rounded()))%")
            .accessibilityAdjustableAction { direction in
                guard available else { return }
                model.setVolume((current?.volume ?? 0) + (direction == .increment ? 0.05 : -0.05))
                model.onVolumeCommit()
            }

            Text("\(Int((level * 100).rounded()))%")
                .font(.system(size: 14, weight: .bold))
                .foregroundColor(MBReferenceStyle.ink)
                .minimumScaleFactor(0.8)
            if showName {
                Text(current?.name ?? "—")
                    .font(.system(size: 9, weight: .regular))
                    .foregroundColor(MBReferenceStyle.ink)
                    .lineLimit(2)
                    .multilineTextAlignment(.center)
                    .minimumScaleFactor(0.7)
            }
        }
        .padding(.horizontal, 5)
        .padding(.top, 6)
        .padding(.bottom, 11)
        .frame(width: width, height: height)
        .background(MBReferenceStyle.white, in: RoundedRectangle(cornerRadius: width / 2, style: .continuous))
    }

    // MARK: - Search
    private func searchPill(height: CGFloat) -> some View {
        HStack(spacing: 12) {
            Image(systemName: "magnifyingglass")
                .font(.system(size: 17, weight: .medium))
                .foregroundColor(MBReferenceStyle.ink)
            TextField("", text: $model.searchText,
                      prompt: Text(t("Buscar música", "Search music"))
                        .foregroundColor(MBReferenceStyle.ink.opacity(0.38)))
                .font(.system(size: 14))
                .foregroundColor(MBReferenceStyle.ink)
                .autocorrectionDisabled()
                .submitLabel(.search)
                .onSubmit { model.onSearch(model.searchText) }
            if !model.searchText.isEmpty {
                Button { model.searchText = "" } label: {
                    Image(systemName: "xmark.circle.fill")
                        .foregroundColor(MBReferenceStyle.ink.opacity(0.5))
                }
                .buttonStyle(.plain)
            }
            Button(action: model.onOpenSettings) {
                Image(systemName: "gearshape")
                    .font(.system(size: 15, weight: .medium))
                    .foregroundColor(MBReferenceStyle.ink.opacity(0.65))
            }
            .buttonStyle(.plain)
            .accessibilityLabel(t("Configuración de MediaBloom", "MediaBloom settings"))
        }
        .padding(.horizontal, 16)
        .frame(height: height)
        .background(MBReferenceStyle.white, in: Capsule())
    }

    // MARK: - Featured mix, original circles
    private func featuredPanel(landscape: Bool) -> some View {
        GeometryReader { size in
            ZStack(alignment: .topLeading) {
                RoundedRectangle(cornerRadius: landscape ? 28 : 29, style: .continuous)
                    .fill(MBReferenceStyle.gray)
                GeometryReader { g in
                    if landscape {
                        Circle().fill(MBReferenceStyle.ink)
                            .frame(width: g.size.height * 0.91, height: g.size.height * 0.91)
                            .position(x: g.size.width * 0.44, y: g.size.height * 0.37)
                        Circle().fill(MBReferenceStyle.coral)
                            .frame(width: g.size.height * 0.39, height: g.size.height * 0.39)
                            .position(x: g.size.width * 0.55, y: g.size.height * 0.37)
                        Circle().fill(MBReferenceStyle.white)
                            .frame(width: g.size.height * 1.33, height: g.size.height * 1.33)
                            .position(x: g.size.width * 0.70, y: g.size.height * 1.10)
                    } else {
                        Circle().fill(MBReferenceStyle.ink)
                            .frame(width: g.size.width * 0.50, height: g.size.width * 0.50)
                            .position(x: g.size.width * 0.69, y: g.size.height * 0.31)
                        Circle().fill(MBReferenceStyle.coral)
                            .frame(width: g.size.width * 0.215, height: g.size.width * 0.215)
                            .position(x: g.size.width * 0.83, y: g.size.height * 0.37)
                        Circle().fill(MBReferenceStyle.white)
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
                .foregroundColor(MBReferenceStyle.ink)
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
                    .foregroundColor(MBReferenceStyle.white)
                    .frame(width: 35, height: 35)
                    .background(MBReferenceStyle.ink, in: Circle())
                Text(musicApp.map { t("Abrir en \($0.name)", "Open in \($0.name)") } ??
                     t("Agrega una app de música", "Add a music app"))
                    .font(.system(size: 12, weight: .medium))
                    .foregroundColor(MBReferenceStyle.ink)
                    .lineLimit(1)
                    .minimumScaleFactor(0.8)
            }
            .padding(.leading, 5)
            .padding(.trailing, 16)
            .frame(height: 45)
            .background(MBReferenceStyle.white, in: Capsule())
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
            .background(MBReferenceStyle.white, in: RoundedRectangle(cornerRadius: 26, style: .continuous))
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
                        .overlay(Capsule().stroke(MBReferenceStyle.ink, lineWidth: 1))
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
                Capsule().fill(MBReferenceStyle.ink).frame(width: 22, height: 2)
                Capsule().fill(MBReferenceStyle.track).frame(width: 22, height: 2)
                Capsule().fill(MBReferenceStyle.track).frame(width: 22, height: 2)
            }
        }
        .foregroundColor(MBReferenceStyle.ink)
    }

    private func artistChip(_ artist: String) -> some View {
        let active = model.favoriteArtists.contains(artist)
        return Button { model.toggleArtist(artist) } label: {
            Text(artist)
                .font(.system(size: 11, weight: .medium))
                .foregroundColor(active ? MBReferenceStyle.white : MBReferenceStyle.ink)
                .lineLimit(1)
                .minimumScaleFactor(0.65)
                .frame(maxWidth: .infinity)
                .frame(height: 25)
                .background(active ? MBReferenceStyle.ink : MBReferenceStyle.white, in: Capsule())
                .overlay(Capsule().stroke(MBReferenceStyle.ink, lineWidth: 1))
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
                        Capsule().fill(index == appPage ? MBReferenceStyle.ink : MBReferenceStyle.track)
                            .frame(width: index == appPage ? 16 : 6, height: 6)
                    }
                }
                .padding(.bottom, 1)
            }
        }
        .foregroundColor(MBReferenceStyle.ink)
        .padding(.horizontal, 10)
        .padding(.top, 13)
        .padding(.bottom, 10)
        .background(MBReferenceStyle.white, in: RoundedRectangle(cornerRadius: 26, style: .continuous))
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
                        .stroke(selected ? MBReferenceStyle.coral : Color.clear, lineWidth: 2)
                        .padding(-3))
            }
            .buttonStyle(.plain)
            .accessibilityLabel(t("Abrir ", "Open ") + app.name)
            Text(app.name)
                .font(.system(size: 11, weight: .medium))
                .lineLimit(1)
                .minimumScaleFactor(0.7)
                .frame(maxWidth: .infinity)
            Button { model.select(app) } label: {
                Text(volumeText(for: app))
                    .font(.system(size: 10))
                    .foregroundColor(MBReferenceStyle.ink)
                    .padding(.horizontal, 7)
                    .frame(height: 17)
                    .background(MBReferenceStyle.pill, in: Capsule())
            }
            .buttonStyle(.plain)
            .accessibilityLabel(t("Seleccionar volumen de ", "Select volume for ") + app.name)
        }
        .frame(maxWidth: .infinity)
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
        guard app.volumeAvailable else { return t("Abrir", "Open") }
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
                                .overlay(Capsule().stroke(MBReferenceStyle.ink, lineWidth: 1))
                            Text(app.name)
                                .font(.system(size: 12, weight: .semibold))
                                .lineLimit(1)
                                .minimumScaleFactor(0.7)
                            Spacer(minLength: 0)
                            Text(volumeText(for: app))
                                .font(.system(size: 11))
                                .lineLimit(1)
                        }
                        .foregroundColor(MBReferenceStyle.ink)
                        .padding(.horizontal, 11)
                        .frame(maxWidth: .infinity)
                        .frame(height: 69)
                        .background(index == chosen.count - 1 ? MBReferenceStyle.white : MBReferenceStyle.gray,
                                    in: RoundedRectangle(cornerRadius: 25, style: .continuous))
                            }
                            .buttonStyle(.plain)
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
                    .foregroundColor(MBReferenceStyle.white)
                    .padding(.horizontal, 10)
                    .padding(.vertical, 4)
                    .background(MBReferenceStyle.ink, in: Capsule())
                Spacer(minLength: 0)
            }
            .padding(.horizontal, 10)
            .padding(.bottom, 10)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(MBReferenceStyle.white, in: RoundedRectangle(cornerRadius: 27, style: .continuous))
    }
}