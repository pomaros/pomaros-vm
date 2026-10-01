// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
// Formats: AmigaOS IFF FTXT/UTF8 and ILBM specifications; see docs/CLIPBOARD.md.
import Foundation

enum ClipboardValue: Equatable {
  case text(String)
  case image(width: Int, height: Int, rgba: [UInt8])
}

enum ClipboardCodec {
  static let maximumBytes = 16 * 1024 * 1024
  static let maximumPixels = 4 * 1024 * 1024
  enum Failure: Error { case malformed, unsupported, tooLarge }

  private static func be16(_ p: [UInt8], _ at: Int) -> Int {
    Int(p[at]) << 8 | Int(p[at + 1])
  }
  private static func be32(_ p: [UInt8], _ at: Int) -> Int {
    Int(p[at]) << 24 | Int(p[at + 1]) << 16 | Int(p[at + 2]) << 8 | Int(p[at + 3])
  }
  private static func bytes(_ n: Int) -> [UInt8] {
    [24, 16, 8, 0].map { UInt8(truncatingIfNeeded: n >> $0) }
  }
  private static func chunk(_ name: String, _ content: [UInt8]) -> [UInt8] {
    Array(name.utf8) + bytes(content.count) + content + (content.count & 1 == 0 ? [] : [0])
  }
  private static func form(_ name: String, _ content: [UInt8]) throws -> [UInt8] {
    guard content.count <= maximumBytes - 12 else { throw Failure.tooLarge }
    return Array("FORM".utf8) + bytes(content.count + 4) + Array(name.utf8) + content
  }

  static func encode(_ value: ClipboardValue) throws -> [UInt8] {
    switch value {
    case .text(let text):
      guard text.utf8.count <= maximumBytes / 2 - 32 else { throw Failure.tooLarge }
      let utf8 = Array(text.utf8)
      // UTF8 preserves every scalar. Old CHRS-only applications receive a
      // Latin-1 fallback, with '?' for characters outside their repertoire.
      let latin = text.unicodeScalars.map { $0.value <= 255 ? UInt8($0.value) : UInt8(63) }
      return try form("FTXT", chunk("CHRS", latin) + chunk("UTF8", utf8))
    case .image(let width, let height, let rgba):
      guard width > 0, height > 0, width <= 4096, height <= 4096,
            width * height <= maximumPixels, rgba.count == width * height * 4 else {
        throw Failure.tooLarge
      }
      let alpha = stride(from: 3, to: rgba.count, by: 4).map { rgba[$0] }
      guard alpha.allSatisfy({ $0 == 0 || $0 == 255 }) else { throw Failure.unsupported }
      let mask = alpha.contains(0), planes = mask ? 25 : 24
      let rowBytes = ((width + 15) / 16) * 2
      var body = [UInt8](repeating: 0, count: rowBytes * planes * height)
      for y in 0..<height {
        for x in 0..<width {
          let pixel = (y * width + x) * 4, bit = UInt8(0x80 >> (x & 7))
          for plane in 0..<24 where rgba[pixel + plane / 8] & (1 << (plane & 7)) != 0 {
            body[(y * planes + plane) * rowBytes + x / 8] |= bit
          }
          if mask && rgba[pixel + 3] != 0 { body[(y * planes + 24) * rowBytes + x / 8] |= bit }
        }
      }
      var header = [UInt8](repeating: 0, count: 20)
      for (at, n) in [(0, width), (2, height), (16, width), (18, height)] {
        header[at] = UInt8(n >> 8); header[at + 1] = UInt8(truncatingIfNeeded: n)
      }
      header[8] = 24; header[9] = mask ? 1 : 0; header[14] = 1; header[15] = 1
      return try form("ILBM", chunk("BMHD", header) + chunk("BODY", body))
    }
  }

  static func decode(_ data: [UInt8]) throws -> ClipboardValue {
    guard data.count <= maximumBytes else { throw Failure.tooLarge }
    guard data.count >= 12, Array(data[0..<4]) == Array("FORM".utf8),
          be32(data, 4) == data.count - 8 else { throw Failure.malformed }
    let kind = String(bytes: data[8..<12], encoding: .ascii)
    var chunks = [(String, [UInt8])](), offset = 12
    while offset < data.count {
      guard data.count - offset >= 8 else { throw Failure.malformed }
      let size = be32(data, offset + 4), start = offset + 8
      guard size <= data.count - start, size + (size & 1) <= data.count - start,
            let name = String(bytes: data[offset..<offset + 4], encoding: .ascii) else {
        throw Failure.malformed
      }
      chunks.append((name, Array(data[start..<start + size])))
      offset = start + size + (size & 1)
      guard chunks.count <= 4096 else { throw Failure.tooLarge }
    }
    if kind == "FTXT" {
      let unicode = chunks.filter { $0.0 == "UTF8" }
      if !unicode.isEmpty {
        guard let text = String(bytes: unicode.flatMap { $0.1 }, encoding: .utf8) else {
          throw Failure.malformed
        }
        return .text(text)
      }
      guard !chunks.contains(where: { $0.0 == "CSET" }) else { throw Failure.unsupported }
      let legacy = chunks.filter { $0.0 == "CHRS" }
      guard !legacy.isEmpty else { throw Failure.unsupported }
      let scalars = legacy.flatMap { $0.1 }.map { UnicodeScalar(UInt32($0))! }
      return .text(String(String.UnicodeScalarView(scalars)))
    }
    guard kind == "ILBM" else { throw Failure.unsupported }
    return try image(chunks)
  }

  private static func image(_ chunks: [(String, [UInt8])]) throws -> ClipboardValue {
    var header: [UInt8]?, palette = [UInt8](), body: [UInt8]?, mode = 0
    for (name, content) in chunks {
      switch name {
      case "BMHD":
        guard body == nil, content.count == 20 else { throw Failure.malformed }
        header = content
      case "CMAP":
        guard body == nil, content.count % 3 == 0, content.count <= 768 else { throw Failure.malformed }
        palette = content
      case "CAMG":
        guard body == nil, content.count == 4 else { throw Failure.malformed }
        mode = be32(content, 0)
      case "BODY":
        guard body == nil, header != nil else { throw Failure.malformed }
        body = content
      case "DEST", "CLUT", "CTBL", "SHAM": throw Failure.unsupported
      default: break
      }
    }
    guard let h = header, let raster = body else { throw Failure.malformed }
    let width = be16(h, 0), height = be16(h, 2), planes = Int(h[8])
    let masking = h[9], compression = h[10], transparent = be16(h, 12)
    guard width > 0, height > 0, width <= 4096, height <= 4096,
          width * height <= maximumPixels else { throw Failure.tooLarge }
    guard ((1...8).contains(planes) || planes == 24), masking <= 2,
          compression <= 1, mode & 0x800 == 0,
          (mode & 0x80 == 0 || planes == 6), !(planes == 24 && masking == 2) else {
      throw Failure.unsupported
    }
    let rowBytes = ((width + 15) / 16) * 2
    let rows = planes + (masking == 1 ? 1 : 0)
    var at = 0, rgba = [UInt8](repeating: 0, count: width * height * 4)
    for y in 0..<height {
      var scan = [UInt8](); scan.reserveCapacity(rows * rowBytes)
      for _ in 0..<rows {
        let end = scan.count + rowBytes
        while scan.count < end {
          guard at < raster.count else { throw Failure.malformed }
          if compression == 0 {
            let count = end - scan.count
            guard count <= raster.count - at else { throw Failure.malformed }
            scan += raster[at..<at + count]; at += count
          } else {
            let code = Int(Int8(bitPattern: raster[at])); at += 1
            if code == -128 { continue }
            let count = code >= 0 ? code + 1 : 1 - code
            guard count <= end - scan.count else { throw Failure.malformed }
            if code >= 0 {
              guard count <= raster.count - at else { throw Failure.malformed }
              scan += raster[at..<at + count]; at += count
            } else {
              guard at < raster.count else { throw Failure.malformed }
              scan += [UInt8](repeating: raster[at], count: count); at += 1
            }
          }
        }
      }
      for x in 0..<width {
        let bit = UInt8(0x80 >> (x & 7)), pixel = (y * width + x) * 4
        var index = 0
        for plane in 0..<planes where scan[plane * rowBytes + x / 8] & bit != 0 { index |= 1 << plane }
        if planes == 24 {
          for channel in 0..<3 { rgba[pixel + channel] = UInt8(truncatingIfNeeded: index >> (channel * 8)) }
        } else {
          let half = mode & 0x80 != 0 && index >= 32
          let color = half ? index - 32 : index
          guard color * 3 + 2 < palette.count else { throw Failure.unsupported }
          for channel in 0..<3 { rgba[pixel + channel] = palette[color * 3 + channel] >> (half ? 1 : 0) }
        }
        let opaque = masking == 1 ? scan[planes * rowBytes + x / 8] & bit != 0 : (masking != 2 || index != transparent)
        rgba[pixel + 3] = opaque ? 255 : 0
      }
    }
    guard at == raster.count else { throw Failure.malformed }
    return .image(width: width, height: height, rgba: rgba)
  }
}
