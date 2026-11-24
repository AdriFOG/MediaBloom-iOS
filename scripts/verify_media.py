#!/usr/bin/env python3
"""Reject any MediaBloom artifact that could be mistaken for primary BloomBox."""
import plistlib
import sys
from pathlib import Path
from zipfile import ZipFile

HOST = "Payload/LiveContainer.app/"
MEDIA_ID = "com.kdt.BloomBoxMedia"
EXPECTED_SCHEME = "bloomboxmedia"
DISALLOWED_IDS = {
    "com.kdt.livecontainer",
    "com.kdt.livecontainer.LiveProcess",
    "com.kdt.livecontainer.ShareExtension",
}


def check(ipa: Path) -> None:
    if not ipa.is_file() or ipa.stat().st_size < 500_000:
        raise RuntimeError(f"Not a complete IPA: {ipa}")
    with ZipFile(ipa) as archive:
        broken = archive.testzip()
        if broken:
            raise RuntimeError(f"Corrupted ZIP member: {broken}")
        names = set(archive.namelist())
        host_path = HOST + "Info.plist"
        if host_path not in names:
            raise RuntimeError(f"Host app missing from {ipa}")
        info = plistlib.loads(archive.read(host_path))
        app_id = info.get("CFBundleIdentifier")
        assert app_id == MEDIA_ID, f"Unsafe host identifier: {app_id!r}"
        assert app_id not in DISALLOWED_IDS
        assert info.get("CFBundleDisplayName") == "BloomBox Media"
        assert info.get("BloomBoxMediaEdition") is True
        primary = info.get("CFBundleIcons", {}).get("CFBundlePrimaryIcon", {})
        assert primary.get("CFBundleIconName") == "AppIcon", "Native MediaBloom primary icon is absent"
        assert HOST + "Assets.car" in names and archive.getinfo(HOST + "Assets.car").file_size > 0, "Missing compiled native asset catalog"

        assert info.get("LSApplicationCategoryType") == "public.app-category.entertainment"
        assert not info.get("GCSupportsGameMode", False)
        schemes = info["CFBundleURLTypes"][0]["CFBundleURLSchemes"]
        assert schemes == [EXPECTED_SCHEME], f"Unsafe URL scheme: {schemes!r}"
        assert "livecontainer" not in schemes, "Cannot hijack primary instance URL scheme"
        other_schemes = [s for item in info["CFBundleURLTypes"]
                         for s in item.get("CFBundleURLSchemes", [])]
        assert "sidestore" not in other_schemes, (
            "MediaBloom must not hijack the main SideStore URL scheme", other_schemes)
        assert "mediabloom-sidestore" in other_schemes or "+SideStore" not in ipa.name
        # Exact same shared group is intentional: guest apps can use LC shared storage.
        alt_groups = info.get("ALTAppGroups", [])
        if "+SideStore" in ipa.name:
            assert "group.com.SideStore.SideStore" in alt_groups, \
                f"Missing SideStore group declaration: {alt_groups}"
        assert HOST + "Frameworks/LiveContainerSwiftUI.framework/LiveContainerSwiftUI" in names
        # ZIP integrity is NOT enough: on Intel macOS the pinned ARM-only
        # dylibify silently failed and packaging omitted the SideStore binary.
        # This must abort CI instead of shipping an un-installable IPA.
        side_root = HOST + "Frameworks/SideStoreApp.framework/"
        side_plist = side_root + "Info.plist"
        assert side_plist in names, "SideStoreApp framework Info.plist missing"
        side_info = plistlib.loads(archive.read(side_plist))
        side_executable = side_info.get("CFBundleExecutable")
        assert side_executable == "SideStore", (
            f"Unexpected SideStore executable: {side_executable!r}"
        )
        side_binary = side_root + side_executable
        assert side_binary in names, f"Missing required SideStore Mach-O: {side_binary}"
        assert archive.getinfo(side_binary).file_size > 1_000_000, (
            "SideStore executable is empty or truncated"
        )
        macho_header = archive.open(side_binary).read(4)
        assert macho_header in (
            b"\\xcf\\xfa\\xed\\xfe",
            b"\\xfe\\xed\\xfa\\xcf",
            b"\\xca\\xfe\\xba\\xbe",
            b"\\xbe\\xba\\xfe\\xca",
        ), f"SideStore executable is not Mach-O: {macho_header!r}"
        # Validate every other packaged framework's CFBundleExecutable too.
        for framework_plist in sorted(n for n in names if n.endswith(".framework/Info.plist")):
            framework_info = plistlib.loads(archive.read(framework_plist))
            executable = framework_info.get("CFBundleExecutable")
            if executable:
                binary = framework_plist[:-len("Info.plist")] + executable
                assert binary in names and archive.getinfo(binary).file_size > 0, (
                    f"Missing framework executable: {binary}"
                )
        plugins = [
            x for x in names
            if x.startswith(HOST + "PlugIns/") and x.endswith(".appex/Info.plist")
        ]
        for plugin_path in plugins:
            plug = plistlib.loads(archive.read(plugin_path))
            plug_id = plug.get("CFBundleIdentifier", "")
            if not plug_id.startswith(MEDIA_ID + "."):
                raise RuntimeError(f"Extension ID can collide with BloomBox: {plugin_path}: {plug_id}")
        # MediaBloom must contain its OWN Control Center extension; a correct
        # host IPA alone would install but expose no control buttons on iOS.
        controls_info_path = HOST + "PlugIns/ManzanaExpiryWidget.appex/Info.plist"
        assert controls_info_path in names, "MediaBloom Control Center extension absent"
        controls = plistlib.loads(archive.read(controls_info_path))
        assert controls.get("CFBundleDisplayName") == "MediaBloom Controles", controls
        assert controls.get("NSExtension", {}).get("NSExtensionPointIdentifier") == "com.apple.widgetkit-extension"
        assert controls.get("CFBundleIdentifier") == MEDIA_ID + ".ManzanaExpiryWidget", controls
        assert HOST + "PlugIns/ManzanaExpiryWidget.appex/ManzanaExpiryWidget" in names, "No media controls executable"

        # Guarantee that OS-localized names do not say BloomBox instead of Media.
        for path in names:
            if path.startswith(HOST) and path.endswith(".lproj/InfoPlist.strings"):
                raw = archive.read(path)
                try:
                    locale = plistlib.loads(raw)
                    name = locale.get("CFBundleName")
                    assert name == "BloomBox Media", (path, name)
                except (ValueError, TypeError):
                    text = raw.decode("utf-8-sig")
                    assert '"BloomBox Media"' in text, (path, text[:90])
        print(f"PASS {ipa.name}: distinct ID {app_id}, {EXPECTED_SCHEME}://, "
              f"media-only, {len(plugins)} isolated extensions, {len(names)} ZIP members")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit("Usage: verify_media.py MediaBloom-SideStore-unsigned.ipa [...]")
    for arg in sys.argv[1:]:
        check(Path(arg))
