#!/usr/bin/env python3
"""Source contract for the MediaBloom-only system Control Center integration."""
from pathlib import Path
import plistlib

root=Path(".")
widget=(root/"ManzanaExpiryWidget/ManzanaExpiryWidget.swift").read_text()
controls=(root/"ManzanaExpiryWidget/MediaBloomControlCenter.swift").read_text()
project=(root/"LiveContainer.xcodeproj/project.pbxproj").read_text()
entitlements=plistlib.loads(
    (root/"ManzanaExpiryWidget/MediaBloomControl.entitlements").read_bytes())
info=plistlib.loads((root/"ManzanaExpiryWidget/Info.plist").read_bytes())
audio=(root/"TweakLoader/TweakLoader.m").read_text()

assert "@main" in widget and "MediaBloomControlBundle" in widget
assert "ManzanaExpiryLiveActivity()" not in widget
assert widget.count("Control()") == 6
assert controls.count(": ControlWidget") == 6
assert controls.count(": AppIntent") == 6
assert controls.count("ControlWidgetButton(action:") == 6
assert controls.count("StaticControlConfiguration(kind:") == 6
for key in (
    "com.google.ios.youtubemusic", "com.google.ios.youtube",
    "MediaBloomAudioGainV1.", "SecTaskCopyValueForEntitlement",
    "com.apple.security.application-groups", "containerURL(forSecurityApplicationGroupIdentifier:",
    "UserDefaults(suiteName:", "defaults.synchronize()",
    "MBMusicLouderControl", "MBMusicQuieterControl", "MBMusicMuteControl",
    "MBVideoLouderControl", "MBVideoQuieterControl", "MBVideoMuteControl",
):
    assert key in controls or key in widget, key
assert "new = min(1, max(0" in controls
assert "mute: true" in controls
assert "get-task-allow" not in entitlements
assert entitlements["com.apple.security.application-groups"] == [
    "group.com.SideStore.SideStore", "group.com.rileytestut.AltStore"
]
assert info["NSExtension"]["NSExtensionPointIdentifier"] == "com.apple.widgetkit-extension"
assert info["CFBundleDisplayName"] == "MediaBloom Controles"
assert project.count("CODE_SIGN_ENTITLEMENTS = ManzanaExpiryWidget/MediaBloomControl.entitlements;") == 2
assert project.count("IPHONEOS_DEPLOYMENT_TARGET = 18.0;") >= 2
assert 'MBInstallAudioHooks();' in audio
assert '[NSUserDefaults.lcSharedDefaults synchronize];' in audio
for bad in (
    "outputVolume =", "MPVolumeView(", "SystemVolumeDidChangeNotification",
    "AVAudioSession.sharedInstance().setActive", "UIApplication.shared.open",
):
    assert bad not in controls, bad
print("PASS: MediaBloom-owned system controls, authorized App Group, no iOS volume override")
