# MediaBloom-iOS

**BloomBox Media** — a separate multimedia launcher for iOS, built on the shared LiveContainer runtime.

## Experimental standalone build

The signed-ready (unsigned) IPA is published in **Releases**, and Actions also produces experimental artifacts named
**MediaBloom-SideStore-unsigned.ipa**. It is intended to be signed with SideStore
or a compatible installer before installation. Stable release is v1.0.0 after physical-device validation. Sign with SideStore or a compatible installer.

- Standalone host ID: \`com.kdt.BloomBoxMedia\`
- Standalone launch URL: \`bloomboxmedia://\`
- Name on iPhone: **BloomBox Media**
- Shares LiveContainer apps and data only through the signed App Group, when
  the two independently installed hosts have compatible signing identities.
- Shows recognized **shared** media apps such as YouTube, YouTube Music, Spotify,
  VLC and other supported multimedia clients.
- Does **not** provide a gaming launcher. Games are blocked from the media host.
- **BloomAudio** volume (YouTube/YouTube Music) includes optional live controls inside apps; verified on iPhone in v0.3.

## Safety / never overwrite your working BloomBox

1. Install only \`MediaBloom-SideStore-unsigned.ipa\` produced by **this repository**.
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
\`LICENSE-ADDITIONAL-TERMS.md\`.
