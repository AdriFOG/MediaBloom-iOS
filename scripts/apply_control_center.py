#!/usr/bin/env python3
"""Add native Control Center buttons using MediaBloom's EXISTING widget target.

Runs only in the disposable MediaBloom build checkout. No primary BloomBox
project, installed app or game bundle is changed.
"""
from pathlib import Path
import plistlib

ROOT = Path(".")
TOOLS = Path("../media-tools")
FOLDER = ROOT / "ManzanaExpiryWidget"
PBX = ROOT / "LiveContainer.xcodeproj/project.pbxproj"
WIDGET = FOLDER / "ManzanaExpiryWidget.swift"
INFO = FOLDER / "Info.plist"
ENTITLEMENTS = FOLDER / "MediaBloomControl.entitlements"


def apply() -> None:
    source = WIDGET.read_text()
    assert "struct ManzanaExpiryWidgetBundle: WidgetBundle" in source
    assert "@main" in source
    assert "MediaBloomControlBundle" not in source
    assert not (FOLDER / "MediaBloomControlCenter.swift").exists()
    (FOLDER / "MediaBloomControlCenter.swift").write_text(
        (TOOLS / "controls/MediaBloomControlCenter.swift").read_text())

    # Don't expose the original primary BloomBox expiry Live Activity in
    # the independent media edition. Reuse its existing WidgetKit target.
    WIDGET.write_text("""import SwiftUI
import WidgetKit

@main
struct MediaBloomControlBundle: WidgetBundle {
    var body: some Widget {
        MBGeneralVolumeControl()
    }
}
""")

    with INFO.open("rb") as file:
        info = plistlib.load(file)
    assert info["NSExtension"]["NSExtensionPointIdentifier"] == "com.apple.widgetkit-extension"
    info["CFBundleDisplayName"] = "MediaBloom Controles"
    info["CFBundleName"] = "MediaBloomControls"
    with INFO.open("wb") as file:
        plistlib.dump(info, file)

    ENTITLEMENTS.write_text("""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>com.apple.security.application-groups</key>
  <array>
    <string>group.com.SideStore.SideStore</string>
    <string>group.com.rileytestut.AltStore</string>
  </array>
</dict></plist>
""")

    project = PBX.read_text()
    anchor = "INFOPLIST_FILE = ManzanaExpiryWidget/Info.plist;"
    assert project.count(anchor) == 2, "Expected two widget build configurations"
    project = project.replace(
        anchor,
        "  CODE_SIGN_ENTITLEMENTS = ManzanaExpiryWidget/MediaBloomControl.entitlements;\n"
        + anchor,
    )
    # Controls are available on iOS 18+. MediaBloom's host can still target
    # earlier iOS versions; the system simply won't expose this extension.
    # The widget deployment target is the ONLY version changed.
    anchor2 = "IPHONEOS_DEPLOYMENT_TARGET = 16.2;"
    assert project.count(anchor2) == 2, "Expected two widget deployment settings"
    project = project.replace(anchor2, "IPHONEOS_DEPLOYMENT_TARGET = 18.0;")
    # The original BloomBox target declares the widget but does not embed it
    # into the application. Without this copy phase, the IPA installs normally
    # yet *no* MediaBloom controls appear in iOS Control Center.
    widget_embed = "E1292AF32DCA3A660065E12D /* LiveProcess.appex in Embed Foundation Extensions */,"
    assert project.count(widget_embed) == 1
    project = project.replace(widget_embed, widget_embed + "\n                BDA10000000000000000000D /* ManzanaExpiryWidget.appex in Embed Foundation Extensions */,")
    PBX.write_text(project)

    # Discoverable instructions in MediaBloom itself. These buttons belong to
    # iOS Control Center and work from third-party apps without BloomBox.
    home = ROOT / "LiveContainerSwiftUI/Views/BloomBox/BloomBoxLauncherRoot.swift"
    home_text = home.read_text()
    en = "Separate volume for YouTube and YouTube Music. Spotify support is an AVFoundation-only beta: controls are available only if a real compatible player is detected. Reopen the app after enabling."
    es = "Volumen independiente para YouTube y YouTube Music. Spotify está en beta: los controles solo se activan si se detecta un reproductor AVFoundation compatible. Vuelve a abrir la app tras activarlo."
    assert home_text.count(en) == 1 and home_text.count(es) == 1
    home_text = home_text.replace(en,
        "Separate volume for YouTube and YouTube Music. Spotify AV volume is a separate experimental switch in Settings; native volume hooks are disabled. Reopen the app after enabling; use the floating speaker or add MediaBloom Volume from iOS Control Center > Add a Control.")
    home_text = home_text.replace(es,
        "Volumen independiente para YouTube y YouTube Music. El volumen AV de Spotify tiene su propio interruptor experimental en Ajustes; se retiró el hook nativo. Reabre la app después de activarlo; usa el altavoz flotante o añade MediaBloom Volumen desde Centro de control > Añadir un control.")
    home.write_text(home_text)

    print("PASS: standalone MediaBloom Control Center widget target + shared App Groups")


if __name__ == "__main__":
    apply()
