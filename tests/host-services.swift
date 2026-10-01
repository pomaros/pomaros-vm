// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
import Foundation
import Darwin

@main struct HostServicesTest {
  static func put(_ p:inout [UInt8],_ at:Int,_ n:UInt32) {
    for i in 0..<4 { p[at+i]=UInt8(truncatingIfNeeded:n>>(8*i)) }
  }
  static func number(_ p:[UInt8],_ at:Int)->UInt32 {
    (0..<4).reduce(UInt32(0)) {$0 | UInt32(p[at+$1]) << ($1*8)}
  }
  static func writeAll(_ fd:Int32,_ p:[UInt8]) {
    var at=0
    while at<p.count {
      let n=p.withUnsafeBytes { Darwin.write(fd,$0.baseAddress!.advanced(by:at),p.count-at) }
      precondition(n>0);at+=n
    }
  }
  static func readAll(_ fd:Int32,_ count:Int)->[UInt8] {
    var p=[UInt8](repeating:0,count:count),at=0
    while at<count {
      let n=p.withUnsafeMutableBytes { Darwin.read(fd,$0.baseAddress!.advanced(by:at),count-at) }
      precondition(n>0);at+=n
    }
    return p
  }
  static func connection(_ server:HostServices)->(Int32,DispatchSemaphore) {
    var fds:[Int32]=[-1,-1];precondition(socketpair(AF_UNIX,SOCK_STREAM,0,&fds)==0)
    let fd=fds[0],other=fds[1],done=DispatchSemaphore(value:0)
    var timeout=timeval(tv_sec:5,tv_usec:0)
    precondition(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,socklen_t(MemoryLayout<timeval>.size))==0)
    DispatchQueue.global().async { server.serve(other);Darwin.close(other);done.signal() }
    return (fd,done)
  }
  static func request(_ fd:Int32,_ op:UInt16,_ id:UInt32,_ data:[UInt8])->(UInt32,[UInt8]) {
    var h=Array("MVHS".utf8)+[1,0,UInt8(truncatingIfNeeded:op),UInt8(op>>8)]+[UInt8](repeating:0,count:12)
    put(&h,8,id);put(&h,12,UInt32(data.count));writeAll(fd,h);writeAll(fd,data)
    let response=readAll(fd,20)
    precondition(Array(response[0..<12])==Array(h[0..<12]))
    return (number(response,16),readAll(fd,Int(number(response,12))))
  }
  static func main() throws {
    let root=FileManager.default.temporaryDirectory.appendingPathComponent("vmservices-test-"+UUID().uuidString)
    defer { try? FileManager.default.removeItem(at:root) }
    let server=try HostServices(directory:root)
    let (fd,done)=connection(server)
    let data=Array((0..<4097).map {UInt8(truncatingIfNeeded:$0*37)})
    precondition(request(fd,2,1,Array("file.bin".utf8)).0==0)
    precondition(request(fd,3,2,data).0==0)
    precondition(request(fd,4,3,[]).0==0)
    let stored=try Data(contentsOf:root.appendingPathComponent("file.bin"));precondition(stored==Data(data))
    precondition(request(fd,2,4,Array("file.bin".utf8)).0==0)
    precondition(request(fd,3,5,[9,9,9]).0==0)
    precondition(request(fd,4,6,[]).0==4)
    precondition(request(fd,5,7,Array("../outside".utf8)).0==1)
    precondition(symlink("file.bin",root.appendingPathComponent("link.bin").path)==0)
    precondition(request(fd,5,8,Array("link.bin".utf8)).0==3)
    precondition(request(fd,5,9,Array("file.bin".utf8)).0==0)
    var wanted=[UInt8](repeating:0,count:4);put(&wanted,0,65536)
    let reply=request(fd,6,10,wanted);precondition(reply.0==0 && reply.1==data)
    precondition(request(fd,7,11,[]).0==0)
    precondition(request(fd,999,12,[]).0==5)
    let idle=request(fd,13,13,[])
    precondition(idle.0==0 && idle.1==[UInt8](repeating:0,count:8))
    precondition(request(fd,14,14,[UInt8](repeating:0,count:12)).0==1)
    precondition(server.shutdownStatus()==nil)
    server.requestShutdown()
    let pending=request(fd,13,15,[])
    precondition(pending.0==0 && pending.1.count==8 && pending.1 != idle.1)
    server.requestShutdown()
    precondition(request(fd,13,16,[]).1==pending.1)
    let failed=pending.1+[1,0,0,0]
    precondition(request(fd,14,17,failed).0==0 && server.shutdownStatus()==1)
    server.requestShutdown()
    let retry=request(fd,13,18,[])
    precondition(retry.1 != pending.1 && server.shutdownStatus()==nil)
    precondition(request(fd,14,19,failed).0==1 && server.shutdownStatus()==nil)
    precondition(request(fd,14,20,retry.1+[0,0,0,0]).0==0 && server.shutdownStatus()==0)
    precondition(request(fd,14,21,retry.1+[0,0,0,0]).0==0)
    precondition(request(fd,14,22,retry.1+[1,0,0,0]).0==2 && server.shutdownStatus()==0)
    Darwin.close(fd);precondition(done.wait(timeout:.now()+5) == .success)
    let names=try FileManager.default.contentsOfDirectory(atPath:root.path)
    precondition(!names.contains(where:{$0.hasPrefix(".upload-")}))
    for bad in 0..<3 {
      let (badFD,finished)=connection(server)
      var h=Array("MVHS".utf8)+[1,0,1,0]+[UInt8](repeating:0,count:12);put(&h,8,1)
      if bad==0 { put(&h,12,65537) }
      if bad==1 { h[4]=2 }
      if bad==2 { put(&h,8,0) }
      writeAll(badFD,h);var byte:UInt8=0;precondition(Darwin.read(badFD,&byte,1)==0)
      Darwin.close(badFD);precondition(finished.wait(timeout:.now()+5) == .success)
    }
    print("host-services result=PASS put,get,no-replace,containment,symlink,abort-cleanup,framing,shutdown-token-retry-idempotence")
  }
}
