import SwiftUI
import Foundation

// MediaBloom's public, keyless discovery stack. Nothing here plays audio or
// changes the guest, SideStore or iOS volume. MusicBrainz requests are throttled
// to one request per second; cached data survives a cold start.
struct MBMusicArtist: Identifiable, Codable, Hashable {
    let id: String
    let name: String
}

struct MBMusicTrack: Identifiable, Codable, Hashable {
    let id: String                  // MusicBrainz recording MBID
    let title: String
    let artist: String              // Verified favorite-artist display name
    let artistID: String
    let album: String
    let coverReleaseID: String?
    let listenCount: Int

    var coverURL: URL? {
        guard let id = coverReleaseID, UUID(uuidString: id) != nil else { return nil }
        return URL(string: "https://coverartarchive.org/release/\(id)/front-250")
    }
}

private actor MBMusicBrainzLimiter {
    static let shared = MBMusicBrainzLimiter()
    private var lastStarted = Date.distantPast
    func waitForSlot() async {
        let delay = max(0, 1.15 - Date().timeIntervalSince(lastStarted))
        if delay > 0 {
            try? await Task.sleep(nanoseconds: UInt64(delay * 1_000_000_000))
        }
        lastStarted = Date()
    }
}

actor MBMusicCatalogAPI {
    static let shared = MBMusicCatalogAPI()
    private let agent = "MediaBloom-iOS/0.4 (https://github.com/AdriFOG/MediaBloom-iOS)"
    private let artistIDsKey = "MediaBloomArtistMBIDsV1"
    private let cacheAge: TimeInterval = 12 * 60 * 60
    private var artistSearchCache: [String: [MBMusicArtist]] = [:]
    private var recordingCache: [String: [MBMusicTrack]] = [:]

    private func fetchJSON(_ url: URL, musicBrainz: Bool = false) async throws -> Any {
        guard url.scheme == "https", let host = url.host?.lowercased(),
              ["musicbrainz.org", "api.listenbrainz.org"].contains(host) else {
            throw URLError(.badURL)
        }
        if musicBrainz { await MBMusicBrainzLimiter.shared.waitForSlot() }
        var request = URLRequest(url: url, cachePolicy: .returnCacheDataElseLoad, timeoutInterval: 16)
        request.setValue(agent, forHTTPHeaderField: "User-Agent")
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        let (data, response) = try await URLSession.shared.data(for: request)
        guard let response = response as? HTTPURLResponse else { throw URLError(.badServerResponse) }
        guard (200..<300).contains(response.statusCode) else {
            throw URLError(response.statusCode == 429 || response.statusCode == 503
                ? .dataNotAllowed : .badServerResponse)
        }
        guard data.count <= 3_000_000 else { throw URLError(.dataLengthExceedsMaximum) }
        return try JSONSerialization.jsonObject(with: data)
    }

    private func validArtist(_ id: String) -> Bool { UUID(uuidString: id) != nil }

    func searchArtists(_ raw: String) async throws -> [MBMusicArtist] {
        let query = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        guard query.count >= 2, query.count <= 70 else { return [] }
        let key = query.folding(options: [.caseInsensitive, .diacriticInsensitive], locale: .current)
        if let cached = artistSearchCache[key] { return cached }
        var url = URLComponents(string: "https://musicbrainz.org/ws/2/artist/")!
        let sanitized = query.replacingOccurrences(of: "\"", with: "")
        url.queryItems = [URLQueryItem(name: "query", value: "artist:\"\(sanitized)\""),
                          URLQueryItem(name: "fmt", value: "json"),
                          URLQueryItem(name: "limit", value: "12")]
        guard let address = url.url else { throw URLError(.badURL) }
        let json = try await fetchJSON(address, musicBrainz: true)
        let candidates = (json as? [String: Any])?["artists"] as? [[String: Any]] ?? []
        var seen = Set<String>()
        let results: [MBMusicArtist] = candidates.compactMap { item in
            guard let id = item["id"] as? String, validArtist(id),
                  let name = item["name"] as? String, !name.isEmpty,
                  seen.insert(id).inserted else { return nil }
            return MBMusicArtist(id: id, name: name)
        }
        artistSearchCache[key] = results
        return results
    }

    func rememberArtist(_ artist: MBMusicArtist) {
        guard validArtist(artist.id) else { return }
        var ids = UserDefaults.standard.dictionary(forKey: artistIDsKey) as? [String: String] ?? [:]
        ids[artist.name.folding(options: [.caseInsensitive, .diacriticInsensitive], locale: .current)] = artist.id
        UserDefaults.standard.set(ids, forKey: artistIDsKey)
    }

    private func resolveArtist(_ name: String) async throws -> MBMusicArtist? {
        let key = name.folding(options: [.caseInsensitive, .diacriticInsensitive], locale: .current)
        let saved = UserDefaults.standard.dictionary(forKey: artistIDsKey) as? [String: String] ?? [:]
        if let id = saved[key], validArtist(id) { return MBMusicArtist(id: id, name: name) }
        let found = try await searchArtists(name)
        // A similarly-named performer must NEVER become a recommendation.
        guard let exact = found.first(where: {
            $0.name.folding(options: [.caseInsensitive, .diacriticInsensitive], locale: .current) == key
        }) else { return nil }
        rememberArtist(exact)
        return exact
    }

    private func topTracks(_ artist: MBMusicArtist) async throws -> [MBMusicTrack] {
        if let cached = recordingCache[artist.id] { return cached }
        let prefKey = "MediaBloomTopTracksV1." + artist.id
        let stampKey = prefKey + ".updated"
        if Date().timeIntervalSince1970 - UserDefaults.standard.double(forKey: stampKey) < cacheAge,
           let data = UserDefaults.standard.data(forKey: prefKey),
           let tracks = try? JSONDecoder().decode([MBMusicTrack].self, from: data) {
            recordingCache[artist.id] = tracks
            return tracks
        }
        guard let url = URL(string: "https://api.listenbrainz.org/1/popularity/top-recordings-for-artist/\(artist.id)") else {
            throw URLError(.badURL)
        }
        let json = try await fetchJSON(url)
        guard let data = json as? [[String: Any]] else { throw URLError(.cannotParseResponse) }
        var seen = Set<String>()
        var tracks: [MBMusicTrack] = []
        for item in data {
            guard let id = item["recording_mbid"] as? String, UUID(uuidString: id) != nil,
                  let title = item["recording_name"] as? String,
                  !title.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty,
                  let artistIDs = item["artist_mbids"] as? [String],
                  artistIDs.contains(artist.id) else { continue }
            let unique = title.folding(options: [.caseInsensitive, .diacriticInsensitive], locale: .current)
            guard seen.insert(unique).inserted else { continue }
            let artwork = item["caa_release_mbid"] as? String
            tracks.append(MBMusicTrack(
                id: id, title: title, artist: artist.name, artistID: artist.id,
                album: item["release_name"] as? String ?? "",
                coverReleaseID: artwork.flatMap { UUID(uuidString: $0) != nil ? $0 : nil },
                listenCount: item["total_listen_count"] as? Int ?? 0))
            if tracks.count == 12 { break }
        }
        tracks.sort { $0.listenCount > $1.listenCount }
        recordingCache[artist.id] = tracks
        if !tracks.isEmpty, let saved = try? JSONEncoder().encode(tracks) {
            UserDefaults.standard.set(saved, forKey: prefKey)
            UserDefaults.standard.set(Date().timeIntervalSince1970, forKey: stampKey)
        }
        return tracks
    }

    func recommendations(for favoriteNames: [String]) async throws -> [MBMusicTrack] {
        let names = Array(Set(favoriteNames.map {
            $0.trimmingCharacters(in: .whitespacesAndNewlines)
        })).filter { !$0.isEmpty }.sorted().prefix(15)
        var groups: [[MBMusicTrack]] = []
        var successful = false
        var lastError: Error?
        for name in names {
            do {
                guard let artist = try await resolveArtist(name) else { continue }
                groups.append(try await topTracks(artist))
                successful = true
            } catch { lastError = error }
        }
        if !successful, let lastError { throw lastError }
        // Interleave favorites instead of letting one massive artist dominate.
        var result: [MBMusicTrack] = []
        var seen = Set<String>()
        for position in 0..<12 {
            for group in groups where position < group.count {
                let song = group[position]
                let key = song.id
                if seen.insert(key).inserted { result.append(song) }
                if result.count >= 30 { return result }
            }
        }
        return result
    }

    func exactSongURL(for song: MBMusicTrack, appID: String) async -> URL? {
        guard UUID(uuidString: song.id) != nil else { return nil }
        var parts = URLComponents(string: "https://musicbrainz.org/ws/2/recording/\(song.id)")!
        parts.queryItems = [URLQueryItem(name: "inc", value: "url-rels"),
                            URLQueryItem(name: "fmt", value: "json")]
        guard let url = parts.url,
              let json = try? await fetchJSON(url, musicBrainz: true) as? [String: Any],
              let links = json["relations"] as? [[String: Any]] else { return nil }
        for link in links {
            guard let metadata = link["url"] as? [String: Any],
                  let value = metadata["resource"] as? String,
                  let target = URL(string: value), target.scheme == "https",
                  let host = target.host?.lowercased() else { continue }
            let path = target.path.lowercased()
            let id = appID.lowercased()
            if id == "com.spotify.client", host == "open.spotify.com", path.hasPrefix("/track/") {
                return target
            }
            if id == "com.google.ios.youtubemusic", host == "music.youtube.com", path == "/watch" {
                return target
            }
            if id == "com.apple.music", host == "music.apple.com", path.contains("/song/") {
                return target
            }
            if id == "com.deezer.deezer", host.hasSuffix("deezer.com"), path.contains("/track/") {
                return target
            }
            if id == "com.google.ios.youtube", (host == "www.youtube.com" || host == "youtube.com"), path == "/watch" {
                return target
            }
        }
        return nil
    }

    // If an exact song is not linked from MusicBrainz, open an HONEST search,
    // never a fabricated song ID or an unrelated video.
    nonisolated static func searchURL(for song: MBMusicTrack, appID: String) -> URL? {
        let query = "\(song.artist) \(song.title)"
        let id = appID.lowercased()
        if id == "com.spotify.client" {
            var parts = URLComponents(string: "https://open.spotify.com/search")!
            parts.path = "/search/" + query
            return parts.url
        }
        let root: String
        let key: String
        switch id {
        case "com.google.ios.youtubemusic": root = "https://music.youtube.com/search"; key = "q"
        case "com.apple.music": root = "https://music.apple.com/us/search"; key = "term"
        case "com.deezer.deezer": root = "https://www.deezer.com/search"; key = "q"
        case "com.google.ios.youtube": root = "https://www.youtube.com/results"; key = "search_query"
        default: return nil
        }
        var parts = URLComponents(string: root)!
        parts.queryItems = [URLQueryItem(name: key, value: query)]
        return parts.url
    }
}

@MainActor final class MBMusicDiscovery: ObservableObject {
    @Published private(set) var suggestions: [MBMusicArtist] = []
    @Published private(set) var searching = false
    @Published private(set) var searchError: String?
    @Published private(set) var songs: [MBMusicTrack] = []
    @Published private(set) var loading = false
    @Published private(set) var loadError: String?
    private var searchVersion = 0
    private var refreshVersion = 0

    func search(_ name: String) {
        searchVersion += 1
        let current = searchVersion
        searching = true
        searchError = nil
        suggestions = []
        Task {
            do {
                let list = try await MBMusicCatalogAPI.shared.searchArtists(name)
                if current == searchVersion { suggestions = list }
            } catch {
                if current == searchVersion { searchError = "No pudimos consultar MusicBrainz." }
            }
            if current == searchVersion { searching = false }
        }
    }

    func choose(_ artist: MBMusicArtist, model: MediaBloomHomeModel) {
        Task { await MBMusicCatalogAPI.shared.rememberArtist(artist) }
        model.addArtist(artist.name)
        if !model.favoriteArtists.contains(artist.name) { model.toggleArtist(artist.name) }
    }

    func refresh(_ favorites: Set<String>) {
        refreshVersion += 1
        let current = refreshVersion
        songs = []
        loadError = nil
        guard !favorites.isEmpty else { loading = false; return }
        loading = true
        Task {
            do {
                let loaded = try await MBMusicCatalogAPI.shared.recommendations(for: Array(favorites))
                if current == refreshVersion { songs = loaded }
            } catch {
                if current == refreshVersion { loadError = "No se pudo consultar el catálogo. Intenta de nuevo." }
            }
            if current == refreshVersion { loading = false }
        }
    }
}

// Decomposed views to avoid huge SwiftUI type-check expressions in the
// existing pinned BloomBox host, as discovered in the previous release.
struct MBMusicRecommendationCard: View {
    let songs: [MBMusicTrack]
    let isLoading: Bool
    let error: String?
    let hasFavorites: Bool
    let playerAvailable: Bool
    let english: Bool
    let theme: MBReferenceTheme
    @Binding var currentIndex: Int
    let onOpenSong: (MBMusicTrack) -> Void
    let onSelectArtists: () -> Void
    let onRetry: () -> Void

    private var song: MBMusicTrack? {
        guard !songs.isEmpty else { return nil }
        return songs[max(0, currentIndex) % songs.count]
    }

    var body: some View {
        HStack(spacing: 7) {
            if let track = song {
                cover(for: track)
                VStack(alignment: .leading, spacing: 3) {
                    Text(track.title)
                        .font(.system(size: 12, weight: .semibold))
                        .lineLimit(1)
                    Text(track.artist)
                        .font(.system(size: 10))
                        .lineLimit(1)
                        .opacity(0.75)
                    Text(english ? "Open track" : "Abrir canción")
                        .font(.system(size: 9, weight: .medium))
                        .opacity(0.7)
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                Button { onOpenSong(track) } label: {
                    Image(systemName: "play.fill")
                        .font(.system(size: 12))
                        .foregroundColor(theme.white)
                        .frame(width: 32, height: 32)
                        .background(theme.ink, in: Circle())
                }
                .disabled(!playerAvailable)
                .accessibilityLabel(english ? "Open track" : "Abrir canción")
                if songs.count > 1 {
                    Button { currentIndex = (currentIndex + 1) % songs.count } label: {
                        Image(systemName: "chevron.right.circle.fill")
                            .font(.system(size: 17))
                    }
                    .accessibilityLabel(english ? "Next suggestion" : "Siguiente recomendación")
                }
            } else if isLoading {
                ProgressView().tint(theme.ink)
                Text(english ? "Finding your music…" : "Buscando tu música…")
                    .font(.system(size: 11))
            } else {
                Button(action: hasFavorites ? onRetry : onSelectArtists) {
                    HStack(spacing: 8) {
                        Image(systemName: hasFavorites ? "arrow.clockwise" : "plus.circle.fill")
                        Text(!hasFavorites
                            ? (english ? "Choose favorite artists" : "Elige tus artistas favoritos")
                            : error != nil
                              ? (english ? "Retry music search" : "Reintentar recomendaciones")
                              : (english ? "No tracks found yet" : "Aún no hay canciones"))
                            .font(.system(size: 11, weight: .medium))
                    }
                }
                .buttonStyle(.plain)
            }
        }
        .foregroundColor(theme.ink)
        .padding(.horizontal, 9)
        .frame(maxWidth: 265)
        .frame(height: 55)
        .background(theme.white, in: RoundedRectangle(cornerRadius: 27, style: .continuous))
        .buttonStyle(.plain)
    }

    @ViewBuilder private func cover(for song: MBMusicTrack) -> some View {
        if let url = song.coverURL {
            AsyncImage(url: url) { state in
                if let image = state.image {
                    image.resizable().scaledToFill()
                } else {
                    Image(systemName: "music.note")
                        .font(.system(size: 19)).foregroundColor(theme.ink)
                }
            }
            .frame(width: 41, height: 41)
            .clipShape(RoundedRectangle(cornerRadius: 12))
        } else {
            Image(systemName: "music.note")
                .foregroundColor(theme.ink)
                .frame(width: 41, height: 41)
                .background(theme.gray, in: RoundedRectangle(cornerRadius: 12))
        }
    }
}

struct MBMusicArtistPicker: View {
    @ObservedObject var model: MediaBloomHomeModel
    @ObservedObject var catalog: MBMusicDiscovery
    let english: Bool
    let onboarding: Bool
    let musicApps: [MediaBloomHomeApp]
    let onFinish: () -> Void
    @State private var search = ""
    @AppStorage("MediaBloomPreferredMusicAppIDV1") private var selectedAppID = ""

    private func t(_ es: String, _ en: String) -> String { english ? en : es }
    private var favorites: [String] { model.favoriteArtists.sorted() }
    private var options: [MBMusicArtist] { catalog.suggestions }

    var body: some View {
        NavigationView {
            ScrollView(.vertical, showsIndicators: false) {
                VStack(alignment: .leading, spacing: 17) {
                    if onboarding {
                        HStack(spacing: 12) {
                            Image(systemName: "heart.circle.fill")
                                .font(.system(size: 42)).foregroundColor(.orange)
                            VStack(alignment: .leading, spacing: 2) {
                                Text(t("Tu música comienza aquí", "Your music starts here"))
                                    .font(.title3.bold())
                                Text(t("Elige 3 o más artistas. Solo te mostraremos canciones de tus favoritos.",
                                       "Pick 3 or more artists. Only your favorites will appear."))
                                    .font(.footnote).foregroundStyle(.secondary)
                            }
                        }
                    }
                    HStack(spacing: 8) {
                        TextField(t("Busca un artista", "Search an artist"), text: $search)
                            .textFieldStyle(.roundedBorder)
                            .autocorrectionDisabled()
                            .textInputAutocapitalization(.words)
                            .submitLabel(.search)
                            .onSubmit(submit)
                        Button(action: submit) { Image(systemName: "magnifyingglass") }
                            .buttonStyle(.borderedProminent)
                            .disabled(search.trimmingCharacters(in: .whitespaces).count < 2)
                    }
                    if catalog.searching { ProgressView() }
                    if let error = catalog.searchError {
                        Text(error).font(.footnote).foregroundColor(.orange)
                    }
                    if !options.isEmpty {
                        Text(t("Resultados de MusicBrainz", "MusicBrainz results"))
                            .font(.caption.weight(.semibold))
                        ForEach(options) { artist in
                            Button {
                                catalog.choose(artist, model: model)
                                search = ""
                            } label: {
                                HStack(spacing: 10) {
                                    Image(systemName: "music.mic")
                                        .foregroundColor(.orange)
                                        .frame(width: 33, height: 33)
                                        .background(Color.gray.opacity(0.15), in: Circle())
                                    Text(artist.name).foregroundColor(.primary)
                                    Spacer()
                                    Image(systemName: favorites.contains(artist.name) ? "checkmark.circle.fill" : "plus.circle")
                                }
                            }
                            .buttonStyle(.plain)
                        }
                    } else if onboarding {
                        Text(t("Para empezar, prueba con:", "Try searching for:"))
                            .font(.caption).foregroundStyle(.secondary)
                        HStack(spacing: 7) {
                            ForEach(["Bad Bunny", "The Weeknd", "Billie Eilish"], id: \.self) { name in
                                Button(name) { search = name; submit() }
                                    .font(.caption)
                                    .buttonStyle(.bordered)
                            }
                        }
                    }
                    if !favorites.isEmpty {
                        Text(t("TUS ARTISTAS", "YOUR ARTISTS"))
                            .font(.caption.weight(.semibold))
                        ForEach(favorites, id: \.self) { name in
                            HStack(spacing: 9) {
                                Image(systemName: "checkmark.circle.fill").foregroundColor(.green)
                                Text(name)
                                Spacer()
                                Button {
                                    model.toggleArtist(name)
                                } label: { Image(systemName: "minus.circle") }
                                .accessibilityLabel(t("Quitar ", "Remove ") + name)
                            }
                            .font(.subheadline)
                        }
                    }
                    if !musicApps.isEmpty {
                        Text(t("REPRODUCTOR PREFERIDO", "PREFERRED PLAYER"))
                            .font(.caption.weight(.semibold))
                        Picker(t("Abrir canciones con", "Open songs with"), selection: $selectedAppID) {
                            ForEach(musicApps) { app in
                                Text(app.name).tag(app.id)
                            }
                        }
                        .pickerStyle(.menu)
                        Text(t("Si no existe un enlace directo, abriremos la búsqueda de esa canción.",
                               "If no exact track link exists, we will open the track search."))
                            .font(.caption2).foregroundStyle(.secondary)
                    }
                    Button {
                        onFinish()
                    } label: {
                        Text(onboarding ? t("Continuar", "Continue") : t("Listo", "Done"))
                            .frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(onboarding && favorites.count < 3)
                    if onboarding {
                        Button(t("Omitir por ahora", "Skip for now"), action: onFinish)
                            .frame(maxWidth: .infinity)
                    }
                    Text(t("Datos: MusicBrainz y ListenBrainz. Portadas: Cover Art Archive.",
                           "Data: MusicBrainz and ListenBrainz. Art: Cover Art Archive."))
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                        .frame(maxWidth: .infinity)
                }
                .padding(20)
            }
            .navigationTitle(onboarding ? "MediaBloom" : t("Tus artistas", "Your artists"))
            .toolbar {
                if !onboarding {
                    ToolbarItem(placement: .cancellationAction) {
                        Button(t("Cerrar", "Close"), action: onFinish)
                    }
                }
            }
        }
        .navigationViewStyle(.stack)
        .onAppear {
            if selectedAppID.isEmpty || !musicApps.contains(where: { $0.id == selectedAppID }) {
                selectedAppID = musicApps.first(where: { $0.id.lowercased() == "com.google.ios.youtubemusic" })?.id
                    ?? musicApps.first?.id ?? ""
            }
        }
    }

    private func submit() {
        let query = search.trimmingCharacters(in: .whitespacesAndNewlines)
        if query.count >= 2 { catalog.search(query) }
    }
}