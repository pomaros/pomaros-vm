// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
import Foundation
import Darwin
import Virtualization

// MVHS v1: 20-byte LE header (magic, version:u16, op:u16, id:u32,
// length:u32, status:u32), at most 64 KiB per message. File operations are
// relative to a pinned directory descriptor; uploads publish without replace.
@available(macOS 13.0, *)
final class HostServices: NSObject, VZVirtioSocketListenerDelegate {
  let directory: URL
  let rootFD: Int32
  private var active=false
  private let listener=VZVirtioSocketListener()
  init(directory: URL) throws {
    self.directory=directory
    try FileManager.default.createDirectory(at:directory,withIntermediateDirectories:true)
    rootFD=Darwin.open(directory.path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW)
    if rootFD<0 { throw NSError(domain:NSPOSIXErrorDomain,code:Int(errno)) }
    super.init();listener.delegate=self
  }
  deinit { Darwin.close(rootFD) }
  func attach(to device: VZVirtioSocketDevice) { device.setSocketListener(listener,forPort:4051) }
  func listener(_ listener: VZVirtioSocketListener,
                shouldAcceptNewConnection connection: VZVirtioSocketConnection,
                from socketDevice: VZVirtioSocketDevice) -> Bool {
    guard !active else { return false }
    let fd=connection.fileDescriptor
    guard fd>=0, fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK)==0 else { return false }
    var noSignal:Int32=1
    guard setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&noSignal,socklen_t(MemoryLayout<Int32>.size))==0 else { return false }
    active=true
    DispatchQueue.global().async { [self] in
      serve(fd)
      DispatchQueue.main.async { [self] in connection.close();active=false }
    }
    return true
  }
  private func exact(_ fd:Int32,_ data:inout [UInt8],write:Bool)->Bool {
    var offset=0
    let deadline=ProcessInfo.processInfo.systemUptime+15
    while offset<data.count && ProcessInfo.processInfo.systemUptime<deadline {
      var p=pollfd(fd:fd,events:Int16(write ? POLLOUT:POLLIN),revents:0)
      let ready=poll(&p,1,100)
      if ready<0 { if errno==EINTR { continue };return false }
      if ready==0 { continue }
      if p.revents & Int16(POLLERR|POLLNVAL) != 0 { return false }
      let count=data.count-offset
      let n=data.withUnsafeMutableBytes { raw -> Int in
        let pointer=raw.baseAddress!.advanced(by:offset)
        return write ? Darwin.write(fd,pointer,count):Darwin.read(fd,pointer,count)
      }
      if n>0 { offset+=n }
      else if n==0 || (errno != EINTR && errno != EAGAIN) { return false }
    }
    return offset==data.count
  }
  private func number(_ p:[UInt8],_ offset:Int,_ count:Int)->UInt64 {
    var n:UInt64=0
    for i in 0..<count { n |= UInt64(p[offset+i]) << (i*8) }
    return n
  }
  private func put(_ p:inout [UInt8],_ offset:Int,_ value:UInt64,_ count:Int) {
    for i in 0..<count { p[offset+i]=UInt8(truncatingIfNeeded:value >> (i*8)) }
  }
  private func filename(_ p:[UInt8])->String? {
    guard !p.isEmpty,p.count<=255,let name=String(bytes:p,encoding:.utf8),
          !name.hasPrefix("."),!name.contains("/"),!name.contains("\\"),!name.contains(":"),
          !name.unicodeScalars.contains(where:{$0.value<32 || $0.value==127}) else { return nil }
    return name
  }
  // One serial request stream, one upload and one download. No file contents
  // are logged. Errors are explicit protocol status, never partial success.
  func serve(_ fd:Int32) {
    var upload:Int32 = -1, download:Int32 = -1
    var temporary="",destination="",uploaded:UInt64=0,downloaded:UInt64=0,downloadSize:UInt64=0
    defer {
      if upload>=0 { Darwin.close(upload) }
      if download>=0 { Darwin.close(download) }
      if !temporary.isEmpty { unlinkat(rootFD,temporary,0) }
    }
    var lastID:UInt64=0
    while true {
      var header=[UInt8](repeating:0,count:20)
      guard exact(fd,&header,write:false),Array(header[0..<4])==Array("MVHS".utf8),
            number(header,4,2)==1,number(header,16,4)==0 else { return }
      let op=number(header,6,2),id=number(header,8,4),length=number(header,12,4)
      guard length<=65536,id>lastID else { return };lastID=id
      var body=[UInt8](repeating:0,count:Int(length))
      guard exact(fd,&body,write:false) else { return }
      var response=[UInt8](),status:UInt64=0
      switch op {
      case 1:
        if !body.isEmpty { status=1 } else { response=Array("Macaros host services 1".utf8) }
      case 2:
        if upload>=0 { status=2 }
        else if let name=filename(body) {
          temporary=".upload-"+UUID().uuidString;destination=name;uploaded=0
          upload=openat(rootFD,temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,mode_t(0o600))
          if upload<0 { temporary="";status=3 }
        } else { status=1 }
      case 3:
        if upload<0 { status=2 }
        else if body.isEmpty || uploaded+UInt64(body.count)>64*1024*1024 { status=1 }
        else {
          var done=0
          while done<body.count {
            let n=body.withUnsafeBytes { Darwin.write(upload,$0.baseAddress!.advanced(by:done),body.count-done) }
            if n<0 && errno==EINTR { continue }
            if n<=0 { status=3;break };done+=n
          }
          uploaded+=UInt64(done)
          if status != 0 { return } // discard incomplete transaction on IO failure
        }
      case 4:
        if upload<0 || !body.isEmpty { status=2 }
        else if fsync(upload) != 0 { status=3 }
        else if linkat(rootFD,temporary,rootFD,destination,0) != 0 { status=errno==EEXIST ? 4:3 }
        else {
          Darwin.close(upload);upload = -1;unlinkat(rootFD,temporary,0);temporary=""
          response=[UInt8](repeating:0,count:8);put(&response,0,uploaded,8)
        }
      case 5:
        if download>=0 { status=2 }
        else if let name=filename(body) {
          download=openat(rootFD,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK)
          var info=stat()
          if download<0 { status=3 }
          else if fstat(download,&info) != 0 || info.st_mode & S_IFMT != S_IFREG || info.st_size<0 || info.st_size>64*1024*1024 {
            Darwin.close(download);download = -1;status=1
          } else {
            downloaded=0;downloadSize=UInt64(info.st_size)
            response=[UInt8](repeating:0,count:8);put(&response,0,downloadSize,8)
          }
        } else { status=1 }
      case 6:
        if download<0 || body.count != 4 { status=2 }
        else {
          let wanted=number(body,0,4)
          if wanted==0 || wanted>65536 { status=1 }
          else {
            response=[UInt8](repeating:0,count:Int(min(wanted,downloadSize-downloaded)))
            var done=0
            while done<response.count {
              let count=response.count-done
              let n=response.withUnsafeMutableBytes { Darwin.read(download,$0.baseAddress!.advanced(by:done),count) }
              if n<0 && errno==EINTR { continue }
              if n<=0 { status=3;break };done+=n
            }
            if status==0 { downloaded+=UInt64(done) } else { response=[] }
          }
        }
      case 7:
        if download<0 || !body.isEmpty { status=2 }
        else { Darwin.close(download);download = -1 }
      default:status=5
      }
      put(&header,12,UInt64(response.count),4);put(&header,16,status,4)
      guard exact(fd,&header,write:true),exact(fd,&response,write:true) else { return }
    }
  }
}
