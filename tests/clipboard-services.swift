// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
import AppKit
import Darwin

@main struct ClipboardServicesTest {
  static func little(_ n: UInt64, _ count: Int) -> [UInt8] {
    (0..<count).map { UInt8(truncatingIfNeeded: n >> ($0 * 8)) }
  }
  static func number(_ p: [UInt8], _ at: Int, _ count: Int) -> UInt64 {
    (0..<count).reduce(UInt64(0)) { $0 | UInt64(p[at + $1]) << ($1 * 8) }
  }
  static func exact(_ fd: Int32, _ count: Int) -> [UInt8] {
    var result = [UInt8](repeating: 0, count: count), offset = 0
    while offset < count {
      let n = result.withUnsafeMutableBytes { Darwin.read(fd, $0.baseAddress!.advanced(by: offset), count - offset) }
      precondition(n > 0); offset += n
    }
    return result
  }
  static func send(_ fd: Int32, _ bytes: [UInt8]) {
    var offset = 0
    while offset < bytes.count {
      let n = bytes.withUnsafeBytes { Darwin.write(fd, $0.baseAddress!.advanced(by: offset), bytes.count - offset) }
      precondition(n > 0); offset += n
    }
  }
  static func request(_ fd: Int32, _ id: inout UInt64, _ op: UInt64, _ body: [UInt8]) -> (UInt64, [UInt8]) {
    id += 1
    let header = Array("MVHS".utf8) + little(1, 2) + little(op, 2) + little(id, 4) + little(UInt64(body.count), 4) + little(0, 4)
    send(fd, header); send(fd, body)
    let response = exact(fd, 20), length = Int(number(response, 12, 4))
    precondition(Array(response[0..<12]) == Array(header[0..<12]) && length <= 65536)
    return (number(response, 16, 4), exact(fd, length))
  }
  static func main() throws {
    let directory = FileManager.default.temporaryDirectory.appendingPathComponent("vm-clip-wire-" + UUID().uuidString)
    let board = NSPasteboard(name: .init("org.aros.vm.wire." + UUID().uuidString))
    defer { board.releaseGlobally(); try? FileManager.default.removeItem(at: directory) }
    let store = ClipboardStore(pasteboard: board)
    let server = try HostServices(directory: directory, clipboard: store)
    let text = String(repeating: "é😀", count: 10000)
    board.clearContents(); board.setString(text, forType: .string)
    var sockets: [Int32] = [-1, -1]
    precondition(socketpair(AF_UNIX, SOCK_STREAM, 0, &sockets) == 0)
    let client = sockets[0], host = sockets[1]
    var timeout = timeval(tv_sec: 5, tv_usec: 0)
    precondition(setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size)) == 0)
    let complete = DispatchSemaphore(value: 0)
    let closed = DispatchSemaphore(value: 0)
    DispatchQueue.global().async { server.serve(host); Darwin.close(host); closed.signal() }
    DispatchQueue.global().async {
      var id: UInt64 = 0
      let first = request(client, &id, 8, little(UInt64.max, 8))
      precondition(first.0 == 0 && first.1.count == 12)
      let revision = number(first.1, 0, 8), length = Int(number(first.1, 8, 4))
      precondition(length > 65536 && length <= ClipboardCodec.maximumBytes)
      var bytes = [UInt8]()
      while bytes.count < length {
        let part = request(client, &id, 9, little(4096, 4))
        precondition(part.0 == 0 && !part.1.isEmpty); bytes += part.1
      }
      let decoded = try! ClipboardCodec.decode(bytes); precondition(decoded == .text(text))
      let same = request(client, &id, 8, little(revision, 8))
      precondition(same.0 == 0 && number(same.1, 8, 4) == 0)
      let image = try! ClipboardCodec.encode(.image(width: 2, height: 1, rgba: [255,0,0,255,0,255,0,255]))
      precondition(request(client, &id, 10, little(revision, 8) + little(UInt64(image.count), 4)).0 == 0)
      precondition(request(client, &id, 11, image).0 == 0)
      let published = request(client, &id, 12, [])
      precondition(published.0 == 0 && published.1.count == 8)
      let changed = number(published.1, 0, 8); precondition(changed != revision)
      DispatchQueue.main.sync { precondition(board.data(forType: .png) != nil) }
      // A new Mac copy must win over an upload carrying an old revision.
      DispatchQueue.main.sync { board.clearContents(); board.setString("newer Mac copy", forType: .string) }
      precondition(request(client, &id, 10, little(changed, 8) + little(UInt64(image.count), 4)).0 == 0)
      precondition(request(client, &id, 11, image).0 == 0)
      let conflict = request(client, &id, 12, [])
      precondition(conflict.0 == 6 && conflict.1.count == 8)
      precondition(request(client, &id, 10, little(changed, 8) + little(UInt64(ClipboardCodec.maximumBytes + 1), 4)).0 == 1)
      // Abandon a partial upload: it must never reach NSPasteboard.
      precondition(request(client, &id, 10, little(number(conflict.1, 0, 8), 8) + little(UInt64(image.count), 4)).0 == 0)
      precondition(request(client, &id, 11, Array(image.prefix(20))).0 == 0)
      Darwin.close(client)
      precondition(closed.wait(timeout: .now() + 5) == .success)
      complete.signal()
    }
    let deadline = Date(timeIntervalSinceNow: 15)
    while complete.wait(timeout: .now()) == .timedOut {
      precondition(Date() < deadline)
      _ = RunLoop.main.run(mode: .default, before: Date(timeIntervalSinceNow: 0.01))
    }
    precondition(board.string(forType: .string) == "newer Mac copy")
    print("clipboard-services result=PASS multi-frame-unicode,snapshot,PNG-publication,stale-write,limit,abort-cleanup")
  }
}
