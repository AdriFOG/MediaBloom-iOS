// MediaBloom standalone branding. All writes are confined to the isolated engine checkout.
import AppKit
import CoreGraphics
import Foundation

func rgb(_ r: CGFloat, _ g: CGFloat, _ b: CGFloat, _ a: CGFloat = 1) -> NSColor {
    NSColor(calibratedRed: r, green: g, blue: b, alpha: a)
}

func installLayeredIcon() throws {
    let fm = FileManager.default
    let root = URL(fileURLWithPath: fm.currentDirectoryPath, isDirectory: true)
    let source = root.deletingLastPathComponent().appendingPathComponent("media-tools/assets/AppIcon.icon")
    let target = root.appendingPathComponent("AppIcon.icon")
    let info = try PropertyListSerialization.propertyList(from: Data(contentsOf: root.appendingPathComponent("LiveContainer/Info.plist")), format: nil) as? [String: Any]
    guard info?["BloomBoxMediaEdition"] as? Bool == true else {
        fatalError("Refusing to modify a non-MediaBloom source checkout")
    }
    if fm.fileExists(atPath: target.path) { try fm.removeItem(at: target) }
    try fm.copyItem(at: source, to: target)
    let project = root.appendingPathComponent("LiveContainer.xcodeproj/project.pbxproj")
    var data = try String(contentsOf: project, encoding: .utf8)
    func insert(_ anchor: String, _ replacement: String) {
        guard data.components(separatedBy: anchor).count == 2 else { fatalError("Unexpected Xcode project structure: \(anchor)") }
        data = data.replacingOccurrences(of: anchor, with: replacement)
    }
    if !data.contains("B1001C0A0000000000000002 /* AppIcon.icon */") {
        insert("/* Begin PBXBuildFile section */", "/* Begin PBXBuildFile section */\n\t\tB1001C0A0000000000000001 /* AppIcon.icon in Resources */ = {isa = PBXBuildFile; fileRef = B1001C0A0000000000000002 /* AppIcon.icon */; };")
        insert("/* Begin PBXFileReference section */", "/* Begin PBXFileReference section */\n\t\tB1001C0A0000000000000002 /* AppIcon.icon */ = {isa = PBXFileReference; lastKnownFileType = wrapper.icon; path = AppIcon.icon; sourceTree = \"<group>\"; };")
        insert("\t\t\t\tE16D9BA02E1D48DE0068EB63 /* README.md */,", "\t\t\t\tE16D9BA02E1D48DE0068EB63 /* README.md */,\n\t\t\t\tB1001C0A0000000000000002 /* AppIcon.icon */,")
        let phase = "\t\t17DCE99B2C7067EC00731D42 /* Resources */ = {\n\t\t\tisa = PBXResourcesBuildPhase;\n\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = (\n"
        insert(phase, phase + "\t\t\t\tB1001C0A0000000000000001 /* AppIcon.icon in Resources */,\n")
        try data.write(to: project, atomically: true, encoding: .utf8)
    }
}
try installLayeredIcon()

enum BloomAppearance { case normal, dark, tinted }
func drawBloom(in rect: NSRect, appearance: BloomAppearance) {
    let colors: [NSColor]
    switch appearance {
    case .normal: colors = [rgb(0.78039, 0.48235, 0.96471), rgb(0.32157, 0.04314, 0.91373)]
    case .dark: colors = [rgb(0.15, 0.15, 0.18), rgb(0.06, 0.06, 0.08)]
    case .tinted: colors = [rgb(0.25, 0.25, 0.25), rgb(0.10, 0.10, 0.10)]
    }
    NSGradient(colors: colors)!.draw(in: rect, angle: -45)
    let files = ["01_Oreja_izquierda","02_Cuerpo_verde","03_Diadema","04_Auricular_izquierdo","05_Carita","06_Oreja_derecha","07_Auricular_derecho","08_Ojos","09_Play","10_Notas"]
    let tints: [CGFloat] = [0.7,0.69,0.23,0.3,1,0.73,0.3,0.12,1,1]
    for (index, name) in files.enumerated() {
        let path = "AppIcon.icon/Assets/\(name).svg"
        guard let image = NSImage(contentsOfFile: path) else { fatalError("Cannot open Bloom layer: \(path)") }
        if appearance == .tinted {
            let mask = NSImage(size: rect.size, flipped: false) { bounds in
                image.draw(in: bounds, from: .zero, operation: .sourceOver, fraction: 1)
                let gray = tints[index]
                NSColor(white: gray, alpha: 1).setFill()
                bounds.fill(using: .sourceIn)
                return true
            }
            mask.draw(in: rect, from: .zero, operation: .sourceOver, fraction: 1)
        } else {
            image.draw(in: rect, from: .zero, operation: .sourceOver, fraction: 1)
        }
    }
}

func save(_ file: String, width: Int, height: Int, bloom: BloomAppearance = .normal) throws {
    guard let bmp = NSBitmapImageRep(
        bitmapDataPlanes: nil, pixelsWide: width, pixelsHigh: height,
        bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
        isPlanar: false, colorSpaceName: .deviceRGB,
        bytesPerRow: width * 4, bitsPerPixel: 32
    ) else { fatalError("Cannot allocate icon bitmap") }

    guard let context = NSGraphicsContext(bitmapImageRep: bmp) else {
        fatalError("Cannot construct image context")
    }
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = context
    let rect = NSRect(x: 0, y: 0, width: width, height: height)
    drawBloom(in: rect, appearance: bloom)
    context.flushGraphics()
    NSGraphicsContext.restoreGraphicsState()

    guard let data = bmp.representation(using: .png, properties: [:]) else {
        fatalError("Cannot encode PNG")
    }
    let dest = URL(fileURLWithPath: file)
    try FileManager.default.createDirectory(
        at: dest.deletingLastPathComponent(), withIntermediateDirectories: true
    )
    try data.write(to: dest)
}

do {
    let catalog = "Resources/Assets.xcassets/AppIcon.appiconset"
    let variants: [(String, BloomAppearance)] = [("AppIcon1024.png", .normal), ("AppIconDark1024.png", .dark), ("AppIconDark1024 1.png", .tinted)]
    for (name, appearance) in variants {
        try save("\(catalog)/\(name)", width: 1024, height: 1024, bloom: appearance)
    }
    // Generate every app icon from our own vector artwork at build time.
    // Do not keep binary copies from legacy source distributions in Git.
    let greyCatalog = "Resources/Assets.xcassets/AppIconGrey.appiconset"
    let greyVariants: [(String, BloomAppearance)] = [("AppIconGrey1024.png", .normal), ("AppIconGreyDark1024.png", .dark), ("AppIconGreyDark1024 1.png", .tinted)]
    for (name, appearance) in greyVariants {
        try save("\(greyCatalog)/\(name)", width: 1024, height: 1024, bloom: appearance)
    }
    try save("Resources/Branding/AppIcon60x60@2x.png", width: 120, height: 120, bloom: .normal)
    try save("Resources/Branding/AppIcon76x76@2x~ipad.png", width: 152, height: 152, bloom: .normal)
    try save("Resources/Branding/MediaBloom1024.png", width: 1024, height: 1024, bloom: .normal)
    print("Generated MediaBloom mascot and alternate icon fallbacks (1024px, 120px, 152px).")
} catch {
    fputs("MediaBloom icon generation failed: \(error)\n", stderr)
    exit(1)
}

// Fail fast on the same native asset compiler used for archive. Do not silently
// substitute flattened artwork when a layered icon is malformed.
if ProcessInfo.processInfo.environment["GITHUB_ACTIONS"] == "true" {
    let output = FileManager.default.temporaryDirectory.appendingPathComponent("bloom-icon-preflight-" + UUID().uuidString)
    do {
        try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: output) }
        let tool = Process()
        tool.executableURL = URL(fileURLWithPath: "/usr/bin/xcrun")
        tool.arguments = ["actool", "--compile", output.path,
            "--platform", "iphoneos", "--minimum-deployment-target", "15.0",
            "--target-device", "iphone", "--target-device", "ipad",
            "--app-icon", "AppIcon", "--include-all-app-icons",
            "--output-partial-info-plist", output.appendingPathComponent("Info.plist").path,
            "--output-format", "human-readable-text", "--warnings", "--errors",
            "AppIcon.icon", "Resources/Assets.xcassets"]
        try tool.run()
        tool.waitUntilExit()
        guard tool.terminationStatus == 0,
              FileManager.default.fileExists(atPath: output.appendingPathComponent("Assets.car").path) else {
            fatalError("Native Bloom icon actool preflight failed (status \(tool.terminationStatus))")
        }
        print("Native Bloom icon actool preflight succeeded.")
    } catch {
        fputs("Native Bloom icon validation failed: \(error)\n", stderr)
        exit(1)
    }
}
