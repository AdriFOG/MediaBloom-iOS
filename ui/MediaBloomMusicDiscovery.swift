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
        return URL(string: "https://coverartarchive.org/release/\(id)/front-500")
    }
}

// Reserve a distinct slot BEFORE suspension. An actor can receive another
// search while an earlier call is sleeping; updating the timestamp afterward
// allowed simultaneous queries and transient MusicBrainz 503/429 responses.
private actor MBMusicBrainzLimiter {
    static let shared = MBMusicBrainzLimiter()
    private var nextAllowed = Date.distantPast
    func waitForSlot() async {
        let now = Date()
        let reserved = max(now, nextAllowed)
        nextAllowed = reserved.addingTimeInterval(1.25)
        let delay = max(0, reserved.timeIntervalSince(now))
        if delay > 0 {
            try? await Task.sleep(nanoseconds: UInt64(delay * 1_000_000_000))
        }
    }
}

private enum MBMusicServiceError: LocalizedError {
    case http(Int)

    var errorDescription: String? {
        let english = Locale.current.languageCode == "en"
        switch self {
        case .http(403):
            return english ? "Access denied by the music service (HTTP 403)." :
                "El servicio rechazó la solicitud (HTTP 403)."
        case .http(let code) where code == 429 || code == 503:
            return english ? "Music service busy or rate-limited (HTTP \(code)). Please retry." :
                "Servicio ocupado o limitado (HTTP \(code)). Intenta de nuevo."
        case .http(let code):
            return english ? "The music service returned HTTP \(code)." :
                "El servidor musical respondió HTTP \(code)."
        }
    }
}

actor MBMusicCatalogAPI {
    static let shared = MBMusicCatalogAPI()
    private let agent = "MediaBloom-iOS/0.4 (https://github.com/AdriFOG/MediaBloom-iOS)"
    private let artistIDsKey = "MediaBloomArtistMBIDsV1"
    private let cacheAge: TimeInterval = 12 * 60 * 60
    private var artistSearchCache: [String: [MBMusicArtist]] = [:]
    private var recordingCache: [String: [MBMusicTrack]] = [:]

    // Official MusicBrainz MBIDs. These are a SMALL offline fallback for the
    // three onboarding examples, not a fake general artist catalog. They also
    // let preexisting favorites resolve without a network search.
    nonisolated static func verifiedMatches(_ search: String) -> [MBMusicArtist] {
        let query = search.trimmingCharacters(in: .whitespacesAndNewlines)
            .folding(options: [.caseInsensitive, .diacriticInsensitive], locale: .current)
        guard query.count >= 2 else { return [] }
        let examples: [MBMusicArtist] = [
            MBMusicArtist(id: "89aa5ecb-59ad-46f5-b3eb-2d424e941f19", name: "Bad Bunny"),
            MBMusicArtist(id: "c8b03190-306c-4120-bb0b-6f2ebfc06ea9", name: "The Weeknd"),
            MBMusicArtist(id: "f4abc0b5-3f7a-4eff-8f78-ac078dbce533", name: "Billie Eilish")
        ]
        return examples.filter { artist in
            let normalized = artist.name.lowercased()
            return normalized.contains(query) ||
                (normalized == "billie eilish" && query == "billy")
        }
    }

    private func fetchJSON(_ url: URL, musicBrainz: Bool = false) async throws -> Any {
        guard url.scheme == "https", let host = url.host?.lowercased(),
              ["musicbrainz.org", "api.listenbrainz.org"].contains(host) else {
            throw URLError(.badURL)
        }
        var request = URLRequest(url: url, cachePolicy: .returnCacheDataElseLoad, timeoutInterval: 16)
        request.setValue(agent, forHTTPHeaderField: "User-Agent")
        request.setValue("application/json", forHTTPHeaderField: "Accept")

        // One slot for BOTH public APIs. Retry only temporary server/network
        // errors and honor numeric Retry-After (bounded to protect the UI).
        for attempt in 0..<3 {
            await MBMusicBrainzLimiter.shared.waitForSlot()
            do {
                let (data, response) = try await URLSession.shared.data(for: request)
                guard let http = response as? HTTPURLResponse else {
                    throw URLError(.badServerResponse)
                }
                let status = http.statusCode
                if [429, 502, 503, 504].contains(status) && attempt < 2 {
                    let suggested = http.value(forHTTPHeaderField: "Retry-After")
                        .flatMap { Double($0) }
                    let delay = min(8.0, max(1.5, suggested ?? Double((attempt + 1) * 2)))
                    try await Task.sleep(nanoseconds: UInt64(delay * 1_000_000_000))
                    continue
                }
                guard (200..<300).contains(status) else {
                    throw MBMusicServiceError.http(status)
                }
                guard data.count <= 3_000_000 else {
                    throw URLError(.dataLengthExceedsMaximum)
                }
                return try JSONSerialization.jsonObject(with: data)
            } catch let error as URLError {
                let transient: [URLError.Code] = [.timedOut, .networkConnectionLost,
                                                  .cannotConnectToHost, .cannotFindHost]
                if transient.contains(error.code) && attempt < 2 {
                    try await Task.sleep(nanoseconds: UInt64((attempt + 1) * 2) * 1_000_000_000)
                    continue
                }
                throw error
            }
        }
        throw URLError(.timedOut)
    }

    private func validArtist(_ id: String) -> Bool { UUID(uuidString: id) != nil }

    func searchArtists(_ raw: String) async throws -> [MBMusicArtist] {
        let query = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        guard query.count >= 2, query.count <= 70 else { return [] }
        let key = query.folding(options: [.caseInsensitive, .diacriticInsensitive], locale: .current)
        if let cached = artistSearchCache[key] { return cached }
        var url = URLComponents(string: "https://musicbrainz.org/ws/2/artist/")!
        let sanitized = query.replacingOccurrences(of: "\"", with: "")
        // A regular indexed search supports partial names and aliases; the
        // quoted exact-field query was too restrictive for onboarding.
        url.queryItems = [URLQueryItem(name: "query", value: sanitized),
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
        if let official = Self.verifiedMatches(name).first(where: {
            $0.name.folding(options: [.caseInsensitive, .diacriticInsensitive], locale: .current) == key
        }) {
            rememberArtist(official)
            return official
        }
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
        // Immediately offer verified matches while the public API is loading.
        // These are real MBIDs, not invented artist results.
        suggestions = MBMusicCatalogAPI.verifiedMatches(name)
        Task {
            do {
                let list = try await MBMusicCatalogAPI.shared.searchArtists(name)
                if current == searchVersion {
                    let verified = MBMusicCatalogAPI.verifiedMatches(name)
                    let verifiedIDs = Set(verified.map { $0.id })
                    suggestions = verified + list.filter { !verifiedIDs.contains($0.id) }
                }
            } catch {
                if current == searchVersion {
                    // Display real failures (HTTP status or iOS network error).
                    // Only these three artists have preverified offline MBIDs.
                    let verified = MBMusicCatalogAPI.verifiedMatches(name)
                    suggestions = verified
                    let english = Locale.current.languageCode == "en"
                    let intro = "MusicBrainz: "
                    let fallback = verified.isEmpty ? "" :
                        (english ? " Showing verified artists." : " Mostrando artistas verificados.")
                    searchError = intro + error.localizedDescription + fallback
                }
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
// A full-bleed hero: the actual recommended album art occupies the entire
// music card in portrait and landscape. Never invents art or track URLs.
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

    private var currentSong: MBMusicTrack? {
        guard !songs.isEmpty else { return nil }
        return songs[max(0, currentIndex) % songs.count]
    }

    var body: some View {
        GeometryReader { geometry in
            Group {
                if let track = currentSong {
                    fullArtworkCard(track, size: geometry.size)
                } else {
                    emptyCard(size: geometry.size)
                }
            }
            .frame(width: geometry.size.width, height: geometry.size.height)
            .clipShape(RoundedRectangle(cornerRadius: 29, style: .continuous))
        }
    }

    private func fullArtworkCard(_ track: MBMusicTrack, size: CGSize) -> some View {
        ZStack(alignment: .bottomTrailing) {
            coverBackground(track, size: size)

            // Text remains readable on very bright or detailed album artwork.
            LinearGradient(
                stops: [
                    .init(color: .black.opacity(0.66), location: 0),
                    .init(color: .black.opacity(0.08), location: 0.42),
                    .init(color: .black.opacity(0.84), location: 1)
                ],
                startPoint: .top, endPoint: .bottom)

            Button { onOpenSong(track) } label: {
                albumForeground(track, size: size)
                    .frame(width: size.width, height: size.height)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .disabled(!playerAvailable)
            .accessibilityLabel((english ? "Open " : "Abrir ") +
                                track.title + " · " + track.artist)

            if songs.count > 1 {
                Button {
                    currentIndex = (currentIndex + 1) % songs.count
                } label: {
                    Image(systemName: "arrow.right")
                        .font(.system(size: 18, weight: .semibold))
                        .foregroundColor(.white)
                        .frame(width: 44, height: 44)
                        .background(Color.black.opacity(0.44), in: Circle())
                        .overlay(Circle().stroke(Color.white.opacity(0.62), lineWidth: 1))
                }
                .buttonStyle(.plain)
                .padding(.trailing, 18)
                .padding(.bottom, 21)
                .accessibilityLabel(english ? "Next song" : "Siguiente canción")
            }
        }
        .frame(width: size.width, height: size.height)
        .foregroundColor(.white)
    }

    private func albumForeground(_ track: MBMusicTrack, size: CGSize) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            Text(english ? "YOUR FAVORITES MIX" : "MIX DE TUS FAVORITOS")
                .font(.system(size: 13, weight: .bold))
                .tracking(1.0)
            Text(english ? "Songs from your favorite artists" :
                 "Canciones de tus artistas favoritos")
                .font(.system(size: 12, weight: .medium))
                .foregroundColor(Color.white.opacity(0.84))
                .padding(.top, 5)

            Spacer(minLength: 16)

            if !track.album.isEmpty {
                Text(track.album.uppercased())
                    .font(.system(size: 11, weight: .bold))
                    .tracking(0.7)
                    .lineLimit(1)
                    .foregroundColor(Color.white.opacity(0.80))
                    .padding(.bottom, 5)
            }
            Text(track.title)
                .font(.system(size: size.width > 370 ? 29 : 25, weight: .bold))
                .lineLimit(2)
                .minimumScaleFactor(0.76)
                .shadow(color: .black.opacity(0.3), radius: 5)

            Text(track.artist)
                .font(.system(size: 16, weight: .medium))
                .foregroundColor(Color.white.opacity(0.91))
                .lineLimit(1)
                .padding(.top, 5)

            HStack(spacing: 10) {
                Image(systemName: "play.fill")
                    .font(.system(size: 18, weight: .semibold))
                    .foregroundColor(theme.ink)
                    .frame(width: 48, height: 48)
                    .background(Color.white, in: Circle())
                Text(playerAvailable ?
                     (english ? "OPEN SONG" : "ABRIR CANCIÓN") :
                     (english ? "NO MUSIC PLAYER" : "SIN REPRODUCTOR"))
                    .font(.system(size: 11, weight: .semibold))
                    .tracking(0.8)
                    .foregroundColor(.white)
                    .lineLimit(1)
                Spacer(minLength: 0)
            }
            .padding(.top, 16)
        }
        .padding(.horizontal, size.width < 340 ? 20 : 24)
        .padding(.vertical, size.height < 260 ? 16 : 22)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }

    @ViewBuilder
    private func coverBackground(_ track: MBMusicTrack, size: CGSize) -> some View {
        if let artworkURL = track.coverURL {
            AsyncImage(url: artworkURL) { state in
                if let image = state.image {
                    image
                        .resizable()
                        .scaledToFill()
                        .frame(width: size.width, height: size.height)
                        .clipped()
                } else {
                    geometricBackground(size: size)
                }
            }
        } else {
            geometricBackground(size: size)
        }
    }

    // Restore MediaBloom's original circles if this album has no cover art.
    private func geometricBackground(size: CGSize) -> some View {
        ZStack {
            theme.gray
            Circle()
                .fill(theme.ink)
                .frame(width: size.width * 0.83, height: size.width * 0.83)
                .offset(x: size.width * 0.24, y: -size.height * 0.14)
            Circle()
                .fill(theme.coral)
                .frame(width: size.width * 0.36, height: size.width * 0.36)
                .offset(x: size.width * 0.31, y: -size.height * 0.08)
            Circle()
                .fill(theme.white)
                .frame(width: size.width * 1.10, height: size.width * 1.10)
                .offset(x: size.width * 0.31, y: size.height * 0.48)
        }
        .frame(width: size.width, height: size.height)
        .clipped()
    }

    private func emptyCard(size: CGSize) -> some View {
        ZStack(alignment: .topLeading) {
            geometricBackground(size: size)
            VStack(alignment: .leading, spacing: 9) {
                Text(english ? "Your favorites mix" : "Mix de tus favoritos")
                    .font(.system(size: 14, weight: .medium))
                Text(english ? "Based on your favorite artists" :
                     "Basado en tus artistas favoritos")
                    .font(.system(size: 23, weight: .bold))
                    .lineLimit(3)

                Spacer(minLength: 18)

                Button(action: hasFavorites ? onRetry : onSelectArtists) {
                    HStack(spacing: 9) {
                        if isLoading {
                            ProgressView().tint(.white)
                        } else {
                            Image(systemName: hasFavorites ? "arrow.clockwise" : "plus.circle.fill")
                        }
                        Text(isLoading ?
                            (english ? "Finding your music…" : "Buscando tu música…") :
                            !hasFavorites ?
                            (english ? "Choose favorite artists" : "Elige tus artistas favoritos") :
                            error != nil ?
                            (english ? "Retry recommendations" : "Reintentar recomendaciones") :
                            (english ? "No tracks found yet" : "Aún no hay canciones"))
                            .lineLimit(2)
                            .font(.system(size: 13, weight: .medium))
                    }
                    .foregroundColor(.white)
                    .padding(.horizontal, 17)
                    .padding(.vertical, 13)
                    .background(theme.ink, in: Capsule())
                }
                .buttonStyle(.plain)
                .disabled(isLoading)
            }
            .foregroundColor(theme.ink)
            .padding(24)
            .frame(width: size.width, height: size.height, alignment: .topLeading)
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
                        Text(catalog.searchError == nil
                             ? t("Resultados de MusicBrainz", "MusicBrainz results")
                             : t("Artistas verificados sin conexión", "Verified artists while offline"))
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