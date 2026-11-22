// Generate an original, simple MediaBloom icon in all slots expected by Xcode.
// Run from the checked-out BloomBox source root on a macOS Actions runner.
import AppKit
import Foundation

func color(_ r: CGFloat, _ g: CGFloat, _ b: CGFloat) -> NSColor {
    NSColor(calibratedRed: r, green: g, blue: b, alpha: 1)
}

func renderIcon() {
    let background = NSRect(x: 0, y: 0, width: 1024, height: 1024)
    color(0.09, 0.07, 0.18).setFill()
    NSBezierPath(rect: background).fill()

    // Compact lavender display panel, no gradients, reflections or glows.
    let panel = NSBezierPath(roundedRect: NSRect(x: 172, y: 286, width: 680, height: 460),
                             xRadius: 104, yRadius: 104)
    color(0.44, 0.32, 0.84).setFill()
    panel.fill()

    let screen = NSBezierPath(roundedRect: NSRect(x: 215, y: 330, width: 594, height: 372),
                              xRadius: 70, yRadius: 70)
    color(0.15, 0.12, 0.29).setFill()
    screen.fill()

    let triangle = NSBezierPath()
    triangle.move(to: NSPoint(x: 448, y: 423))
    triangle.line(to: NSPoint(x: 448, y: 609))
    triangle.line(to: NSPoint(x: 611, y: 516))
    triangle.close()
    color(0.94, 0.92, 1.00).setFill()
    triangle.fill()

    // Three subtle music lines connect video and music in one emblem.
    color(0.73, 0.67, 0.99).setStroke()
    let bars: [(CGFloat, CGFloat)] = [(245, 288), (210, 198), (175, 116)]
    for (y, length) in bars {
        let line = NSBezierPath()
        line.move(to: NSPoint(x: 512 - length / 2, y: y))
        line.line(to: NSPoint(x: 512 + length / 2, y: y))
        line.lineWidth = 12
        line.lineCapStyle = .round
        line.stroke()
    }
}

func saveIcon(_ path: String, width: Int, height: Int) throws {
    guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: width,
        pixelsHigh: height, bitsPerSample: 8, samplesPerPixel: 4,
        hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
        bytesPerRow: width * 4, bitsPerPixel: 32),
        let ctx = NSGraphicsContext(bitmapImageRep: rep)
    else { fatalError("Cannot allocate MediaBloom icon bitmap") }

    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = ctx
    ctx.cgContext.scaleBy(x: CGFloat(width) / 1024, y: CGFloat(height) / 1024)
    renderIcon()
    ctx.flushGraphics()
    NSGraphicsContext.restoreGraphicsState()

    guard let png = rep.representation(using: .png, properties: [:]) else {
        fatalError("Cannot encode MediaBloom PNG")
    }
    let url = URL(fileURLWithPath: path)
    try FileManager.default.createDirectory(at: url.deletingLastPathComponent(),
                                            withIntermediateDirectories: true)
    try png.write(to: url)
}

do {
    let normal = "Resources/Assets.xcassets/AppIcon.appiconset"
    let grey = "Resources/Assets.xcassets/AppIconGrey.appiconset"
    for name in ["AppIcon1024.png", "AppIconDark1024.png", "AppIconDark1024 1.png"] {
        try saveIcon("\(normal)/\(name)", width: 1024, height: 1024)
    }
    for name in ["AppIconGrey1024.png", "AppIconGreyDark1024.png", "AppIconGreyDark1024 1.png"] {
        try saveIcon("\(grey)/\(name)", width: 1024, height: 1024)
    }
    try saveIcon("Resources/Branding/AppIcon60x60@2x.png", width: 120, height: 120)
    try saveIcon("Resources/Branding/AppIcon76x76@2x~ipad.png", width: 152, height: 152)
    try saveIcon("Resources/Branding/MediaBloom1024.png", width: 1024, height: 1024)
    print("Original MediaBloom icon generated: dark purple video/music symbol.")
} catch {
    fputs("Failed to generate MediaBloom icons: \(error)\n", stderr)
    exit(1)
}
