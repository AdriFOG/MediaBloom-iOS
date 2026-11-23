#!/usr/bin/env python3
"""Contract: ONE native Control Center volume button in MediaBloom."""
from pathlib import Path
import plistlib

root = Path(".")
widget = (root/"ManzanaExpiryWidget/ManzanaExpiryWidget.swift").read_text()
controls = (root/"ManzanaExpiryWidget/MediaBloomControlCenter.swift").read_text()
project = (root/"LiveContainer.xcodeproj/project.pbxproj").read_text()
entitlements = plistlib.loads(
    (root/"ManzanaExpiryWidget/MediaBloomControl.entitlements").read_bytes()
)
info = plistlib.loads((root/"ManzanaExpiryWidget/Info.plist").read_bytes())
audio = (root/"TweakLoader/TweakLoader.m").read_text()

assert "@main" in widget and "MediaBloomControlBundle" in widget
assert "ManzanaExpiryLiveActivity()" not in widget
assert widget.count("Control()") == 1, "Six old actions must be removed from gallery"
assert "MBGeneralVolumeControl()" in widget
assert controls.count(": ControlWidget") == 1
assert controls.count(": AppIntent") == 1
assert controls.count("ControlWidgetButton(action:") == 1
assert controls.count("StaticControlConfiguration(") == 1
assert controls.count("provider: MBVolumeProvider()") == 1
assert "MBGeneralVolumeIntent" in controls
assert '"com.kdt.bloomboxmedia.master.volume.v2"' in controls
assert '"com.kdt.bloomboxmedia.master.volume.v2"' in (root/"LiveContainerSwiftUI/Views/BloomBox/BloomBoxLauncherRoot.swift").read_text()
assert "static let openAppWhenRun: Bool = false" in controls
assert "func currentValue() async throws -> Int" in controls

# Native SF Symbols are mandatory: raster Label(image:) displayed '?' on iOS.
assert "Label(" in controls and "systemImage: MBMasterVolume.icon(for: value)" in controls
assert "image: " not in controls
for glyph in (
    "speaker.slash.fill", "speaker.fill", "speaker.wave.1.fill",
    "speaker.wave.2.fill", "speaker.wave.3.fill"
):
    assert glyph in controls, glyph
assert "case 0:" in controls and "case 25:" in controls
assert "case 50:" in controls and "case 75:" in controls
assert "max(0, current - 25)" in controls
assert "current == 0 ? 100" in controls
assert 'let masterKey = "MediaBloomMasterGainV1"' in controls
assert controls.count('defaults.set(slider, forKey:') == 2
assert '"com.google.ios.youtubemusic"' in controls
assert '"com.google.ios.youtube"' in controls
assert '"MediaBloomAudioGainV1." + guest' in controls
assert "defaults.synchronize()" in controls
# WidgetKit refreshes button controls after perform() returns, not before.
assert "ControlCenter.shared.reloadControls(ofKind:" not in controls
assert "return currentLevel(defaults: try sharedDefaults())" in controls
assert "MBMasterVolume.level()" in controls
# Regression: throwing from a ControlValueProvider made both controls blank.
assert "case -1: return \"exclamationmark.triangle.fill\"" in controls
assert "return -1" in controls
assert 'Label(value < 0 ? "MediaBloom · Sin acceso"' in controls
assert "return currentLevel(defaults: try sharedDefaults())" in controls
assert "func currentValue() async throws -> Int" in controls
provider_section = controls.split("private struct MBVolumeProvider: ControlValueProvider", 1)[1].split("struct MBGeneralVolumeIntent:",1)[0]
assert "try MBMasterVolume.level()" not in provider_section
assert "throws -> Int" in provider_section
assert "MediaBloomControlTapCountV1" in controls

# Actual SideStore-provisioned groups can have a Team ID suffix.
assert "kSecAttrAccessGroup" in controls
assert "SecItemCopyMatching" in controls and "SecItemAdd" in controls
assert 'groupBases.map { $0 + "." + id }' in controls
assert "containerURL(forSecurityApplicationGroupIdentifier:" in controls
assert "appendingPathComponent(\"Apps\")" in controls
assert 'object(forKey: "MediaBloomAudioProbeEnabledV1") != nil' in controls
assert "?? found.first" not in controls
assert "throw MBControlError.groupUnavailable" in controls
assert "SecTaskCreateFromSelf" not in controls
assert "SecTaskCopyValueForEntitlement" not in controls

assert "get-task-allow" not in entitlements
assert entitlements["com.apple.security.application-groups"] == [
    "group.com.SideStore.SideStore", "group.com.rileytestut.AltStore"
]
assert info["NSExtension"]["NSExtensionPointIdentifier"] == "com.apple.widgetkit-extension"
assert info["CFBundleDisplayName"] == "MediaBloom Controles"
assert project.count("CODE_SIGN_ENTITLEMENTS = ManzanaExpiryWidget/MediaBloomControl.entitlements;") == 2
assert project.count("BDA10000000000000000000D /* ManzanaExpiryWidget.appex in Embed Foundation Extensions */") == 2
assert project.count("IPHONEOS_DEPLOYMENT_TARGET = 18.0;") >= 2
assert 'MBInstallAudioHooks();' in audio
assert '[NSUserDefaults.lcSharedDefaults synchronize];' in audio
home = (root/"LiveContainerSwiftUI/Views/BloomBox/BloomBoxLauncherRoot.swift").read_text()
assert 'import WidgetKit' in home
assert '@objc(MediaBloomControlCenterSync)' in home
assert 'ControlCenter.shared.reloadControls(' in home
assert 'MediaBloomMasterGainV1' in home
assert 'controlCenterTapCount' in home
assert 'controlCenterGain' in home
assert 'MediaBloomMasterGainV1' in audio
assert 'NSClassFromString(@"MediaBloomControlCenterSync")' in audio
assert 'refreshSelector' in audio
for bad in (
    "outputVolume =", "MPVolumeView(", "SystemVolumeDidChangeNotification",
    "AVAudioSession.sharedInstance().setActive", "UIApplication.shared.open"
):
    assert bad not in controls, bad
print("PASS: ONE stateful MediaBloom control with five supported native SF Symbols, matched shared gain keys and signing-aware group lookup")
