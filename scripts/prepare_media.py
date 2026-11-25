#!/usr/bin/env python3
"""Prepare a separate MediaBloom host from a pinned BloomBox experimental source.

All edits happen in the GitHub Actions checkout, NOT in the user's existing
BloomBox installation, its main branch, or the stable IPA.
"""
from pathlib import Path
import plistlib
import re
import subprocess

ROOT = Path(".")
PIN = "991db585243aa8c2b1ccb454d3d2ea92ea30d7d6"
IDENTIFIER = "com.kdt.BloomBoxMedia"
SCHEME = "bloomboxmedia"


def rewrite(path: str, old: str, new: str) -> None:
    target = ROOT / path
    data = target.read_text()
    if old not in data:
        raise RuntimeError(f"Expected source contract missing: {path}: {old}")
    target.write_text(data.replace(old, new))


def main() -> None:
    actual = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
    if actual != PIN:
        raise RuntimeError(f"Unexpected BloomBox source {actual}; expected pinned {PIN}")
    # This MUST be a cloned media-only build, never the primary BloomBox ID.
    assert IDENTIFIER.lower() != "com.kdt.livecontainer"

    rewrite(
        "xcconfigs/Global.xcconfig",
        "LIVECONTAINER_BUNDLE_IDENTIFIER = com.kdt.livecontainer$(DEVELOPMENT_TEAM_SUFFIX)",
        f"LIVECONTAINER_BUNDLE_IDENTIFIER = {IDENTIFIER}$(DEVELOPMENT_TEAM_SUFFIX)",
    )
    rewrite(
        "LiveContainer.xcodeproj/project.pbxproj",
        "com.kdt.livecontainer",
        IDENTIFIER,
    )
    rewrite(
        ".github/build_github.sh",
        "sidestore-com.kdt.livecontainer",
        "sidestore-com.kdt.BloomBoxMedia",
    )
    rewrite(
        ".github/build_github.sh",
        'CFBundleURLSchemes:0 string sidestore"',
        'CFBundleURLSchemes:0 string mediabloom-sidestore"',
    )
    rewrite(
        ".github/build_github.sh",
        "com.kdt.livecontainer.LiveWidget",
        IDENTIFIER + ".LiveWidget",
    )

    # The pinned LC packaging script historically swallows failed commands.
    # In particular, a CPU-incompatible dylibify can remove SideStore's
    # executable yet still produce a ZIP that passes a basic archive check.
    # Add fail-fast checks *only* in the temporary MediaBloom checkout.
    packaging = ROOT / ".github/build_github.sh"
    script = packaging.read_text()
    if "set -eu\n" not in script:
        script = "set -eu\n" + script
    script = script.replace(
        "rm -r .zsign_cache\n",
        "rm -rf .zsign_cache\n",
    )
    script = script.replace(
        'find payloadlc/Payload -type d -name "_CodeSignature" -exec rm -r {} +',
        'if [ -d payloadlc/Payload ]; then\n'
        '  find payloadlc/Payload -type d -name "_CodeSignature" -exec rm -r {} +\n'
        'fi',
    )
    hook = 'ldid -S"" ./Payload/LiveContainer.app/Frameworks/SideStoreApp.framework/SideStore\n'
    if script.count(hook) != 1:
        raise RuntimeError("Could not find SideStore executable signing step")
    script = script.replace(
        hook,
        hook +
        'test -s ./Payload/LiveContainer.app/Frameworks/SideStoreApp.framework/SideStore || '
        '{ echo "MediaBloom: missing SideStore executable; packaging aborted" >&2; exit 1; }\n'
        'file ./Payload/LiveContainer.app/Frameworks/SideStoreApp.framework/SideStore '
        '| grep -q "Mach-O" || '
        '{ echo "MediaBloom: SideStore is not a Mach-O executable" >&2; exit 1; }\n',
    )
    packaging.write_text(script)
    rewrite(
        ".github/sidelc/LiveWidgetExtension_adhoc.xml",
        "com.kdt.livecontainer.LiveWidget",
        IDENTIFIER + ".LiveWidget",
    )

    path = ROOT / "LiveContainer/Info.plist"
    with path.open("rb") as fh:
        info = plistlib.load(fh)
    info["CFBundleDisplayName"] = "BloomBox Media"
    info["CFBundleName"] = "BloomBox Media"
    # Independent MediaBloom version. The upstream BloomBox host provides
    # 3.3.1/33001; keep a higher build number for existing installations.
    info["CFBundleShortVersionString"] = "2.4.0"
    info["CFBundleVersion"] = "34000"
    info["BloomBoxMediaEdition"] = True
    info["LSApplicationCategoryType"] = "public.app-category.entertainment"
    info["GCSupportsGameMode"] = False
    info["LSSupportsGameMode"] = False
    info["GCSupportsControllerUserInteraction"] = False
    # The first URL type is the one recognized by LC's Multi-LC dispatcher.
    types = info["CFBundleURLTypes"]
    assert len(types) > 0 and types[0]["CFBundleURLSchemes"] == ["livecontainer"]
    types[0]["CFBundleURLSchemes"] = [SCHEME]
    types[0]["CFBundleURLName"] = IDENTIFIER + ".urlscheme"
    queries = info.setdefault("LSApplicationQueriesSchemes", [])
    for item in ["livecontainer", "livecontainer2", "livecontainer3", SCHEME]:
        if item not in queries:
            queries.append(item)
    with path.open("wb") as fh:
        plistlib.dump(info, fh, fmt=plistlib.FMT_XML)

    # iOS normally prefers localized InfoPlist.strings over host display name.
    strings = sorted((ROOT / "Resources").glob("*.lproj/InfoPlist.strings"))
    assert strings, "Missing localized host names"
    for path in strings:
        value = path.read_text()
        value2 = re.sub(
            r'("CFBundleName"\s*=\s*")[^"]*(";\s*)',
            r'\1BloomBox Media\2',
            value,
        )
        if value2 == value:
            raise RuntimeError(f"No localizable bundle name in {path}")
        path.write_text(value2)
    print(f"Media-only build identity ready: {IDENTIFIER} / {SCHEME}://")
    print("Shared SideStore/AltStore App Groups were left intact.")
    print("Primary BloomBox code and installed data have not been modified.")


if __name__ == "__main__":
    main()
