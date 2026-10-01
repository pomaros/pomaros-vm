// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
import Foundation
@main struct StorageTests {
  static func rejected(_ operation:()throws->VMStorage) {
    do { _=try operation();preconditionFailure("storage admission unexpectedly succeeded") }
    catch {}
  }
  static func main()throws {
    let files=FileManager.default
    let root=files.temporaryDirectory.appendingPathComponent("vm-storage-"+UUID().uuidString)
    try files.createDirectory(at:root,withIntermediateDirectories:true)
    defer { try? files.removeItem(at:root) }
    let seed=root.appendingPathComponent("seed.img"),state=root.appendingPathComponent("VM")
    try Data("base system".utf8).write(to:seed)
    var storage:VMStorage?=try VMStorage(state:state,seed:seed)
    let disk=storage!.disk
    let original=try Data(contentsOf:seed),copied=try Data(contentsOf:disk)
    precondition(copied==original)
    rejected { try VMStorage(state:state,seed:seed) }
    let writer=try FileHandle(forWritingTo:disk)
    try writer.seekToEnd();try writer.write(contentsOf:Data(" personal files".utf8));try writer.close()
    storage=nil
    try Data("new application base".utf8).write(to:seed)
    storage=try VMStorage(state:state,seed:seed)
    let preserved=try Data(contentsOf:disk)
    precondition(preserved==Data("base system personal files".utf8))
    storage=nil
    let failed=root.appendingPathComponent("Failed")
    rejected { try VMStorage(state:failed,seed:root.appendingPathComponent("missing.img")) }
    precondition(!files.fileExists(atPath:failed.appendingPathComponent("system.img").path))
    let leftovers=try files.contentsOfDirectory(atPath:failed.path)
    precondition(leftovers==[".launch.lock"])
    storage=try VMStorage(state:failed,seed:seed);storage=nil
    let link=root.appendingPathComponent("link.img")
    try files.createSymbolicLink(at:link,withDestinationURL:seed)
    rejected { try VMStorage(state:root.appendingPathComponent("Linked"),disk:link) }
    print("vm-storage result=PASS first-copy,preserve-personal-disk,exclusive-state,failed-copy-retry,symlink-refusal")
  }
}
