<p align="center">
  <img src="assets/MediaBloom-icon.svg" width="160" alt="MediaBloom app icon — Bloom wearing headphones">
</p>
<h1 align="center">MediaBloom</h1>
<p align="center">A standalone multimedia launcher for iOS, built on the shared LiveContainer runtime.</p>
<p align="center">
  <a href="https://github.com/AdriFOG/MediaBloom-iOS/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/AdriFOG/MediaBloom-iOS?color=8b5cf6&label=release"></a>
</p>
<p align="center">
  <a href="https://github.com/AdriFOG/MediaBloom-iOS/releases/download/v2.0.1/MediaBloom-SideStore-unsigned.ipa"><strong>Download IPA</strong></a> ·
  <a href="https://github.com/AdriFOG/MediaBloom-iOS/releases/tag/v2.0.1">Release notes</a>
</p>

## Meet Bloom 🎧

MediaBloom's new app icon features Bloom wearing headphones, with ten vector layers and native light, dark, tinted and Liquid Glass appearances on supported iOS versions. Older versions use compatible icon fallbacks. The [original icon source](assets/AppIcon.icon) is included in this repository.

## Standalone build

The signed-ready (unsigned) IPA is published in **Releases**, and Actions also produces experimental artifacts named
**MediaBloom-SideStore-unsigned.ipa**. It is intended to be signed with SideStore
or a compatible installer before installation. The current stable release is **v2.0.1 — Meet Bloom**.

- Standalone host ID: `com.kdt.BloomBoxMedia`
- Standalone launch URL: `bloomboxmedia://`
- Name on iPhone: **BloomBox Media**
- Shares LiveContainer apps and data only through the signed App Group, when
  the two independently installed hosts have compatible signing identities.
- Shows recognized **shared** media apps such as YouTube, YouTube Music, Spotify,
  VLC and other supported multimedia clients.
- Does **not** provide a gaming launcher. Games are blocked from the media host.
- **BloomAudio** volume (YouTube/YouTube Music) includes optional live controls inside apps; verified on iPhone in v0.3.

## Safety / never overwrite your working BloomBox

1. Install only `MediaBloom-SideStore-unsigned.ipa` produced by **this repository**.
2. Do **not** install a BloomBox primary IPA just to obtain the Media edition.
3. The build verifies its bundle ID is different from BloomBox and checks its
   distinct URL scheme and embedded extension identifiers before publishing an artifact.
4. Do not delete or reinstall BloomBox. The Media edition is a separate iOS app.
5. After installing, convert selected media guests to **Shared App** in the
   primary BloomBox instance, then reopen Media.

The initial builder pins a known experimental BloomBox engine commit. MediaBloom
maintains its own brand, metadata, UI routing and build verification; it does not
modify BloomBox's main branch. Native device compatibility is still being tested.

Source licenses: AGPL-3.0 and original upstream copyright notices are retained.
Additional original BloomBox terms are in the source snapshot's
`LICENSE-ADDITIONAL-TERMS.md`.
