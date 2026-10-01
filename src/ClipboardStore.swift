// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
import AppKit
import ImageIO
import UniformTypeIdentifiers

// All NSPasteboard access is on the launcher's main queue. Tests use a private
// named pasteboard: qualification never replaces the user's general clipboard.
final class ClipboardStore {
  enum Failure: Error { case changed, unavailable }
  private let pasteboard: NSPasteboard
  private static let iffType = NSPasteboard.PasteboardType("org.aros.clipboard.iff")
  init(pasteboard: NSPasteboard) { self.pasteboard = pasteboard }
  var revision: UInt64 {
    precondition(Thread.isMainThread)
    return UInt64(bitPattern: Int64(pasteboard.changeCount))
  }

  func snapshot(after previous: UInt64?) throws -> (UInt64, [UInt8]?) {
    let token = revision
    if previous == token { return (token, nil) }
    let content: [UInt8]
    if let raw = pasteboard.data(forType: Self.iffType) {
      guard raw.count <= ClipboardCodec.maximumBytes else { throw ClipboardCodec.Failure.tooLarge }
      content = Array(raw); _ = try ClipboardCodec.decode(content)
    } else if let raw = pasteboard.data(forType: .png) ?? pasteboard.data(forType: .tiff) {
      content = try ClipboardCodec.encode(Self.readImage(raw))
    } else if let text = pasteboard.string(forType: .string) {
      content = try ClipboardCodec.encode(.text(text))
    } else if pasteboard.pasteboardItems?.isEmpty != false {
      content = try ClipboardCodec.encode(.text(""))
    } else { throw ClipboardCodec.Failure.unsupported }
    guard revision == token else { throw Failure.changed }
    return (token, content)
  }

  func publish(_ content: [UInt8], replacing token: UInt64) throws -> UInt64 {
    precondition(Thread.isMainThread)
    let value = try ClipboardCodec.decode(content)
    let item = NSPasteboardItem()
    switch value {
    case .text(let text):
      guard item.setString(text, forType: .string) else { throw Failure.unavailable }
    case .image(let width, let height, let rgba):
      guard let provider = CGDataProvider(data: Data(rgba) as CFData),
            let space = CGColorSpace(name: CGColorSpace.sRGB),
            let image = CGImage(width: width, height: height, bitsPerComponent: 8,
              bitsPerPixel: 32, bytesPerRow: width * 4, space: space,
              bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue),
              provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent) else {
        throw Failure.unavailable
      }
      let output = NSMutableData()
      guard let writer = CGImageDestinationCreateWithData(output, UTType.png.identifier as CFString, 1, nil) else {
        throw Failure.unavailable
      }
      CGImageDestinationAddImage(writer, image, nil)
      guard CGImageDestinationFinalize(writer), item.setData(output as Data, forType: .png) else {
        throw Failure.unavailable
      }
    }
    guard item.setData(Data(content), forType: Self.iffType) else { throw Failure.unavailable }
    // Avoid overwriting a Mac copy that happened while the guest uploaded.
    // NSPasteboard has no cross-process compare-and-swap: this narrows that
    // race but does not claim transactional exclusion of other Mac apps.
    guard revision == token else { throw Failure.changed }
    pasteboard.clearContents()
    guard pasteboard.writeObjects([item]) else { throw Failure.unavailable }
    return revision
  }

  private static func readImage(_ data: Data) throws -> ClipboardValue {
    guard data.count <= ClipboardCodec.maximumBytes,
          let source = CGImageSourceCreateWithData(data as CFData, nil),
          CGImageSourceGetCount(source) == 1,
          let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
          let width = properties[kCGImagePropertyPixelWidth] as? Int,
          let height = properties[kCGImagePropertyPixelHeight] as? Int,
          width > 0, height > 0, width <= 4096, height <= 4096,
          width * height <= ClipboardCodec.maximumPixels else { throw ClipboardCodec.Failure.tooLarge }
    if let orientation = properties[kCGImagePropertyOrientation] as? Int, orientation != 1 {
      throw ClipboardCodec.Failure.unsupported
    }
    guard let image = CGImageSourceCreateImageAtIndex(source, 0, nil),
          let space = CGColorSpace(name: CGColorSpace.sRGB) else { throw Failure.unavailable }
    var rgba = [UInt8](repeating: 0, count: width * height * 4)
    let drawn = rgba.withUnsafeMutableBytes { raw -> Bool in
      guard let context = CGContext(data: raw.baseAddress, width: width, height: height,
        bitsPerComponent: 8, bytesPerRow: width * 4, space: space,
        bitmapInfo: CGBitmapInfo.byteOrder32Big.rawValue | CGImageAlphaInfo.premultipliedLast.rawValue) else { return false }
      context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
      return true
    }
    guard drawn else { throw Failure.unavailable }
    // ILBM supports an optional one-bit mask. Refuse partial alpha instead of
    // silently flattening a translucent PNG against an arbitrary background.
    for at in stride(from: 3, to: rgba.count, by: 4) where rgba[at] != 0 && rgba[at] != 255 {
      throw ClipboardCodec.Failure.unsupported
    }
    return .image(width: width, height: height, rgba: rgba)
  }
}
