// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
import Foundation
import Darwin

// Own the state lock through asynchronous shutdown. VZ owns the disk lease.
final class VMStorage {
  let disk:URL
  let state:URL
  private var stateLock:Int32 = -1
  private static func failure(_ message:String)->NSError {
    NSError(domain:"PomAROSVM",code:1,userInfo:[NSLocalizedDescriptionKey:message])
  }
  private static func openFile(_ url:URL,create:Bool,exclusive:Bool)throws->Int32 {
    let fd=Darwin.open(url.path,O_RDWR|O_NOFOLLOW|(create ? O_CREAT:0),mode_t(0o600))
    guard fd>=0 else { throw failure("Cannot open \(url.lastPathComponent): \(String(cString:strerror(errno)))") }
    var info=stat()
    guard fstat(fd,&info)==0,info.st_mode & S_IFMT == S_IFREG,
          (!exclusive || flock(fd,LOCK_EX|LOCK_NB)==0) else {
      Darwin.close(fd)
      throw failure("This VM is already in use, or its storage is not a regular file.")
    }
    return fd
  }
  init(state:URL,disk existing:URL?=nil,seed:URL?=nil)throws {
    self.state=state;self.disk=existing ?? state.appendingPathComponent("system.img")
    let files=FileManager.default
    try files.createDirectory(at:state,withIntermediateDirectories:true)
    stateLock=try Self.openFile(state.appendingPathComponent(".launch.lock"),create:true,exclusive:true)
    do {
      if existing==nil && !files.fileExists(atPath:disk.path) {
        guard let seed else { throw Self.failure("The application has no base system image.") }
        let temporary=state.appendingPathComponent(".system-"+UUID().uuidString+".tmp")
        defer { try? files.removeItem(at:temporary) }
        try files.copyItem(at:seed,to:temporary)
        try files.setAttributes([.posixPermissions:0o600],ofItemAtPath:temporary.path)
        let fd=Darwin.open(temporary.path,O_RDWR|O_NOFOLLOW)
        guard fd>=0 else { throw Self.failure("Cannot finish creating the VM disk.") }
        let synced=fsync(fd)==0;Darwin.close(fd)
        guard synced else { throw Self.failure("Cannot save the new VM disk.") }
        // Publication happens only after the complete copy, under the state lock.
        try files.moveItem(at:temporary,to:disk)
      }
      // A flock on the disk itself conflicts with VZ's own attachment lease.
      let fd=try Self.openFile(disk,create:false,exclusive:false)
      Darwin.close(fd)
    } catch {
      Darwin.close(stateLock);stateLock = -1
      throw error
    }
  }
  deinit {
    if stateLock>=0 { Darwin.close(stateLock) }
  }
}
