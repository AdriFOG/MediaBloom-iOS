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
        assert info.get("LSApplicationCategoryType") == "public.app-category.entertainment"
        assert not info.get("GCSupportsGameMode", False)
        schemes = info["CFBundleURLTypes"][0]["CFBundleURLSchemes"]
        assert schemes == [EXPECTED_SCHEME], f"Unsafe URL scheme: {schemes!r}"
        assert "livecontainer" not in schemes, "Cannot hijack primary instance URL scheme"
        # Exact same shared group is intentional: guest apps can use LC shared storage.
        alt_groups = info.get("ALTAppGroups", [])
        if "+SideStore" in ipa.name:
            assert "group.com.SideStore.SideStore" in alt_groups, \
                f"Missing SideStore group declaration: {alt_groups}"
        assert HOST + "Frameworks/LiveContainerSwiftUI.framework/LiveContainerSwiftUI" in names
        plugins = [
            x for x in names
            if x.startswith(HOST + "PlugIns/") and x.endswith(".appex/Info.plist")
        ]
        for plugin_path in plugins:
            plug = plistlib.loads(archive.read(plugin_path))
            plug_id = plug.get("CFBundleIdentifier", "")
            if not plug_id.startswith(MEDIA_ID + "."):
                raise RuntimeError(f"Extension ID can collide with BloomBox: {plugin_path}: {plug_id}")
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
