# MediaBloom — favorites-only music discovery (experimental)

## Public, keyless providers

- **MusicBrainz** — search and canonical artist MBIDs, plus per-recording external URL relationships. Meaningful User-Agent, at most one MusicBrainz request per second per client. [Official API](https://musicbrainz.org/doc/MusicBrainz_API).
- **ListenBrainz** — `GET /1/popularity/top-recordings-for-artist/{mbid}`, returning ranked recordings with recording name, MBID, credited artist MBIDs and sometimes the cover-art source MBID. [Official API](https://listenbrainz.readthedocs.io/en/latest/users/api/popularity.html).
- **Cover Art Archive** — `GET /release/{mbid}/front-250` for **album covers**, only when a valid source MBID exists. Missing cover art uses a local fallback. [Official API](https://musicbrainz.org/doc/Cover_Art_Archive/API). Album art is not a photograph of the performer.

No Last.fm, Spotify, MusicBrainz or other service credentials are bundled in the app. The public services' use conditions, current rate limits, and artwork rights should be reviewed before commercial distribution.

## UX and behavior

- On the first launch with **no existing favorites**, show artist selection. Users may search verified MusicBrainz artists, select three or more and continue, or skip. Examples are search suggestions only, **never auto-selected**.
- Users already having favorites are migrated without overwriting choices. Artist names and canonical IDs are stored locally. The existing favorites section can reopen the search editor.
- Suggestions include **only recordings credited to one of the selected artist MBIDs**. Favorites removed from the UI are removed from the recommendation query. No similar artists or other algorithmic expansion.
- Recommended tracks are interleaved per artist, limited and cached for 12 hours to minimize requests. The art uses CAA where available and a placeholder otherwise. External providers may be unavailable, and the user sees a retry state.
- The user chooses a music **guest installed and shared** with MediaBloom. A song tap first checks MusicBrainz for a matching real track URL that belongs to that exact player (Spotify, YouTube Music, YouTube, Deezer, or Apple Music). If none exists, the fallback opens that song's **search results**, and does not pretend to play an exact recording.
- Launches via the existing `LCAppModel.runApp(multitask:false,urlStr:)` path so it does not hijack iOS system URLs or alter the BloomBox host, JIT, SideStore, the guest's private data, or BloomAudio.

## Testing

The CI compiles the original side-loadable MediaBloom IPA after overlaying the Swift sources into the **pinned** LiveContainer engine and runs static source/contract checks first. An iPhone test is still needed to verify each guest application handles its search and track URL scheme, especially when an app is already running. The build does **not** prove deep-link playback in every app.