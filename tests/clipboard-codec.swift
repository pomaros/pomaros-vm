// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
import AppKit

@main struct ClipboardCodecTest {
  static func chunk(_ name: String, _ body: [UInt8]) -> [UInt8] {
    Array(name.utf8) + [24,16,8,0].map { UInt8(truncatingIfNeeded: body.count >> $0) } + body + (body.count & 1 == 0 ? [] : [0])
  }
  static func form(_ name: String, _ body: [UInt8]) -> [UInt8] { chunk("FORM", Array(name.utf8) + body) }
  static func reject(_ data: [UInt8]) {
    do { _ = try ClipboardCodec.decode(data); fatalError("accepted invalid clipboard") } catch {}
  }
  static func main() throws {
    let text = ClipboardValue.text("été — 日本語 😀\n")
    let encoded = try ClipboardCodec.encode(text)
    let decoded = try ClipboardCodec.decode(encoded); precondition(decoded == text)
    let legacy = form("FTXT", chunk("CHRS", [0xe9]) + chunk("ANNO", [1]) + chunk("CHRS", [0xf1]))
    let legacyText = try ClipboardCodec.decode(legacy); precondition(legacyText == .text("éñ"))
    reject(form("FTXT", chunk("UTF8", [0xc0, 0xaf])))
    reject(form("FTXT", chunk("CSET", [0,0,0,106]) + chunk("CHRS", [65])))
    for count in 0..<encoded.count { reject(Array(encoded.prefix(count))) }
    var overflow = encoded; overflow[16] = 0xff; reject(overflow)

    // Independent fixture: two indexed pixels, red and green. One plane and
    // one mask row, ByteRun1 literal packets, 16-bit padded scan lines.
    var header: [UInt8] = [0,2,0,1,0,0,0,0,1,1,1,0,0,0,1,1,0,2,0,1]
    let cmap: [UInt8] = [255,0,0,0,255,0]
    let raster: [UInt8] = [1,0x40,0,1,0x80,0]
    let fixture = form("ILBM", chunk("BMHD", header) + chunk("CMAP", cmap) + chunk("BODY", raster))
    let pixels = try ClipboardCodec.decode(fixture)
    precondition(pixels == .image(width: 2, height: 1, rgba: [255,0,0,255,0,255,0,0]))
    let packed = try ClipboardCodec.encode(pixels)
    let unpacked = try ClipboardCodec.decode(packed); precondition(unpacked == pixels)
    // Repeat packet cannot spill into the following plane's row.
    reject(form("ILBM", chunk("BMHD", header) + chunk("CMAP", cmap) + chunk("BODY", [0xfe,0,1,0,0])))
    header[9] = 0; header[10] = 0
    reject(form("ILBM", chunk("BMHD", header) + chunk("CAMG", [0,0,8,0]) + chunk("BODY", [0,0])))
    for count in 0..<fixture.count { reject(Array(fixture.prefix(count))) }
    let wide = ClipboardValue.image(width: 17, height: 2, rgba: (0..<34).flatMap { i -> [UInt8] in
      [UInt8(i), UInt8(255-i), UInt8(i*3), 255]
    })
    let wideDecoded = try ClipboardCodec.decode(ClipboardCodec.encode(wide)); precondition(wideDecoded == wide)
    do {
      _ = try ClipboardCodec.encode(.image(width: 1, height: 1, rgba: [1,2,3,127]))
      fatalError("partial alpha was silently lost")
    } catch {}

    let board = NSPasteboard(name: .init("org.aros.vm.test." + UUID().uuidString))
    defer { board.releaseGlobally() }
    let store = ClipboardStore(pasteboard: board)
    let first = try store.publish(encoded, replacing: store.revision)
    precondition(board.string(forType: .string) == "été — 日本語 😀\n")
    let unchanged = try store.snapshot(after: first); precondition(unchanged.1 == nil)
    board.clearContents(); board.setString("Mac → AROS", forType: .string)
    do { _ = try store.publish(encoded, replacing: first); fatalError("stale clipboard replaced a newer copy") } catch {}
    precondition(board.string(forType: .string) == "Mac → AROS")
    let macText = try store.snapshot(after: first)
    let macDecoded = try ClipboardCodec.decode(macText.1!); precondition(macDecoded == .text("Mac → AROS"))
    _ = try store.publish(try ClipboardCodec.encode(wide), replacing: store.revision)
    let png = board.data(forType: .png)! // Re-import actual PNG without our custom IFF type.
    board.clearContents(); board.setData(png, forType: .png)
    let image = try store.snapshot(after: nil)
    let imageDecoded = try ClipboardCodec.decode(image.1!); precondition(imageDecoded == wide)
    let before = store.revision
    do { _ = try store.publish([0,1,2], replacing: before); fatalError("invalid clipboard replaced PNG") } catch {}
    precondition(store.revision == before && board.data(forType: .png) == png)
    print("clipboard-codec result=PASS unicode,latin1,chunk-bounds,ILBM,mask,ByteRun1,padding,PNG,named-pasteboard,stale-write")
  }
}
