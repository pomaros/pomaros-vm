// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
import AppKit
import Virtualization

let bundledLaunch = CommandLine.arguments.count==1 || CommandLine.arguments[1].hasPrefix("--")
let suppliedOptions=Array(CommandLine.arguments.dropFirst(bundledLaunch ? 1:3))
let launchOptions=Array(Set((bundledLaunch ? ["--network","--host-services"] : []) + suppliedOptions))

@available(macOS 13.0, *)
final class Application: NSObject, NSApplicationDelegate, NSWindowDelegate, VZVirtualMachineDelegate, VZVirtioSocketListenerDelegate {
  var hostServices: HostServices?
  var socketListener: VZVirtioSocketListener?
  var socketProbeAccepted=0
  var machine: VZVirtualMachine!
  var window: NSWindow!
  var machineView: VZVirtualMachineView?
  let serialInput = Pipe()
  let headless = launchOptions.contains("--headless")
  var terminationSignal: DispatchSourceSignal?
  var inputProbeTimer: DispatchSourceTimer?
  var shutdownPoll: DispatchSourceTimer?
  var terminationPending=false
  var allowTermination=false
  var stopping=false
  let storage: VMStorage
  var disk:URL { storage.disk }
  var state:URL { storage.state }
  init(storage:VMStorage) { self.storage=storage }
  func listener(_ listener: VZVirtioSocketListener,
                shouldAcceptNewConnection connection: VZVirtioSocketConnection,
                from socketDevice: VZVirtioSocketDevice) -> Bool {
    guard socketProbeAccepted<2 else { return false }
    socketProbeAccepted+=1
    let fd=connection.fileDescriptor
    guard fd>=0, fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK)==0 else { return false }
    var noSignal:Int32=1
    _ = setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&noSignal,socklen_t(MemoryLayout<Int32>.size))
    // Only the explicitly enabled probe port: 64 KiB per connection, partial stream IO,
    // bounded time. No clipboard access or host files exposed by this probe.
    DispatchQueue.global().async {
      var bytes=[UInt8](repeating:0,count:65536)
      var received=0, written=0
      let deadline=ProcessInfo.processInfo.systemUptime+10
      while ProcessInfo.processInfo.systemUptime<deadline && written<65536 {
        var p=pollfd(fd:fd,events:Int16(received<65536 ? POLLIN : POLLOUT),revents:0)
        let ready=poll(&p,1,100)
        if ready<0 { if errno==EINTR { continue }; break }
        if ready==0 { continue }
        if p.revents & Int16(POLLERR|POLLNVAL) != 0 { break }
        let n:Int
        if received<65536 {
          n=bytes.withUnsafeMutableBytes { read(fd,$0.baseAddress!.advanced(by:received),65536-received) }
          if n>0 { received+=n }
        } else {
          n=bytes.withUnsafeBytes { write(fd,$0.baseAddress!.advanced(by:written),65536-written) }
          if n>0 { written+=n }
        }
        if n==0 { break }
        if n<0 && errno != EAGAIN && errno != EINTR { break }
      }
      let success=received==65536 && written==65536
      DispatchQueue.main.async {
        print(success ? "[HOST] vsock 64KiB stream echo PASS" : "[HOST] vsock 64KiB stream echo FAIL")
        fflush(stdout);connection.close()
      }
    }
    return true
  }
  func applicationDidFinishLaunching(_ notification: Notification) {
    do {
      try FileManager.default.createDirectory(at: state, withIntermediateDirectories: true)
      let config = VZVirtualMachineConfiguration()
      config.platform = VZGenericPlatformConfiguration()
      config.cpuCount = 1
      config.memorySize = 512 * 1024 * 1024
      let boot = VZEFIBootLoader()
      let nvram = state.appendingPathComponent("nvram.bin")
      if FileManager.default.fileExists(atPath: nvram.path) {
        boot.variableStore = VZEFIVariableStore(url: nvram)
      } else {
        boot.variableStore = try VZEFIVariableStore(creatingVariableStoreAt: nvram)
      }
      config.bootLoader = boot
      config.storageDevices = [
        VZVirtioBlockDeviceConfiguration(
          attachment:
            try VZDiskImageStorageDeviceAttachment(url: disk, readOnly: false))
      ]
      let console = VZVirtioConsoleDeviceConfiguration()
      let port = VZVirtioConsolePortConfiguration()
      port.isConsole = true
      port.name = "aros.console"
      port.attachment = VZFileHandleSerialPortAttachment(
        fileHandleForReading: serialInput.fileHandleForReading,
        fileHandleForWriting: .standardOutput)
      console.ports[0] = port
      config.consoleDevices = [console]
      if !headless || launchOptions.contains("--gpu") || launchOptions.contains("--gpu-window") {
        let graphics = VZVirtioGraphicsDeviceConfiguration()
        graphics.scanouts = [
          VZVirtioGraphicsScanoutConfiguration(widthInPixels: 1024, heightInPixels: 768)
        ]
        config.graphicsDevices = [graphics]
      }
      if !headless || launchOptions.contains("--usb-input") {
        config.keyboards = [VZUSBKeyboardConfiguration()]
        config.pointingDevices = [VZUSBScreenCoordinatePointingDeviceConfiguration()]
      }
      if launchOptions.contains("--network") {
        let network=VZVirtioNetworkDeviceConfiguration()
        let macFile=state.appendingPathComponent("mac-address.txt")
        if FileManager.default.fileExists(atPath:macFile.path) {
          let saved=try String(contentsOf:macFile,encoding:.utf8).trimmingCharacters(in:.whitespacesAndNewlines)
          guard let mac=VZMACAddress(string:saved), mac.isUnicastAddress, mac.isLocallyAdministeredAddress else {
            throw NSError(domain:"MacarosVM",code:1,userInfo:[NSLocalizedDescriptionKey:"Invalid saved VM MAC address"])
          }
          network.macAddress=mac
        } else {
          network.macAddress=VZMACAddress.randomLocallyAdministered()
          try (network.macAddress.string+"\n").write(to:macFile,atomically:true,encoding:.utf8)
        }
        network.attachment=VZNATNetworkDeviceAttachment()
        config.networkDevices=[network]
        print("network-config attachment=NAT mac=\(network.macAddress.string)")
        fflush(stdout)
      }
      if launchOptions.contains("--vsock-probe") || launchOptions.contains("--host-services") {
        config.socketDevices=[VZVirtioSocketDeviceConfiguration()]
      }
      config.entropyDevices = [VZVirtioEntropyDeviceConfiguration()]
      try config.validate()
      machine = VZVirtualMachine(configuration: config)
      machine.delegate = self
      if launchOptions.contains("--host-services"),
         let device=machine.socketDevices.first as? VZVirtioSocketDevice {
        let pasteboard: NSPasteboard
        if launchOptions.contains("--clipboard-test") {
          pasteboard=NSPasteboard(name:NSPasteboard.Name("org.aros.vm.test."+UUID().uuidString))
          pasteboard.clearContents()
          guard pasteboard.setString("Macaros clipboard qualification",forType:.string) else {
            throw NSError(domain:"MacarosClipboard",code:1)
          }
          print("clipboard-test private named pasteboard seeded");fflush(stdout)
        } else { pasteboard = .general }
        let services=try HostServices(directory:state.appendingPathComponent("Shared",isDirectory:true),
                                      clipboard:ClipboardStore(pasteboard:pasteboard))
        services.attach(to:device);hostServices=services
        print("host-services port=4051 directory=\(services.directory.path)");fflush(stdout)
      }
      if launchOptions.contains("--vsock-probe"),
         let device=machine.socketDevices.first as? VZVirtioSocketDevice {
        let listener=VZVirtioSocketListener();listener.delegate=self
        socketListener=listener;device.setSocketListener(listener,forPort:4050)
      }
      if !headless || launchOptions.contains("--gpu-window") {
        let view = VZVirtualMachineView()
        view.virtualMachine = machine
        machineView = view
        view.capturesSystemKeys = true
        window = NSWindow(
          contentRect: NSRect(x: 0, y: 0, width: 1024, height: 768),
          styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered,
          defer: false)
        window.title = "Macaros VM"
        window.delegate = self
        window.contentView = view
        window.center()
        window.makeKeyAndOrderFront(nil)
        print("window-id=\(window.windowNumber)")
        fflush(stdout)
        NSApp.activate(ignoringOtherApps: true)


      }
      signal(SIGTERM, SIG_IGN)
      terminationSignal = DispatchSource.makeSignalSource(signal: SIGTERM, queue: .main)
      terminationSignal?.setEventHandler { [weak self] in
        guard let self else { return }
        if !self.terminationPending { NSApp.terminate(nil) }
      }
      terminationSignal?.resume()
      machine.start { result in
        switch result {
        case .success:
          print("vm-start backend=Virtualization.framework result=PASS guest_boot=UNVERIFIED")
          fflush(stdout)
          if launchOptions.contains("--input-probe") {
            let timer=DispatchSource.makeTimerSource(queue:.main)
            self.inputProbeTimer=timer
            timer.schedule(deadline:.now(),repeating:0.1)
            timer.setEventHandler {
              let request=self.state.appendingPathComponent("input.request")
              if FileManager.default.fileExists(atPath:request.path) {
                timer.cancel()
                self.probeInput()
              }
            }
            timer.resume()
          }
        case .failure(let error): self.fail(error)
        }
      }
    } catch { fail(error) }
  }
  // Test events stay inside this application's VZ view; no global event posting.
  func probeInput() {
    guard let view = machineView, let window else { return }
    window.makeFirstResponder(view)
    let events: [(NSEvent.EventType, Double, Double)] = [
      (.mouseMoved, 300, 468), (.mouseMoved, 400, 418),
      (.leftMouseDown, 400, 418), (.leftMouseUp, 400, 418)]
    for (index, item) in events.enumerated() {
      DispatchQueue.main.asyncAfter(deadline: .now()+Double(index)*0.2) {
        guard let event=NSEvent.mouseEvent(with: item.0,
          location: NSPoint(x:item.1,y:item.2), modifierFlags: [],
          timestamp: ProcessInfo.processInfo.systemUptime, windowNumber: window.windowNumber,
          context:nil, eventNumber:index, clickCount:1, pressure:0) else { return }
        if item.0 == .mouseMoved { view.mouseMoved(with:event) }
        else if item.0 == .leftMouseDown { view.mouseDown(with:event) }
        else { view.mouseUp(with:event) }
      }
    }
    for (index, type) in [NSEvent.EventType.keyDown, .keyUp].enumerated() {
      DispatchQueue.main.asyncAfter(deadline: .now()+1+Double(index)*0.2) {
        guard let event=NSEvent.keyEvent(with:type,location:.zero,modifierFlags:[],
          timestamp:ProcessInfo.processInfo.systemUptime,windowNumber:window.windowNumber,
          context:nil,characters:"a",charactersIgnoringModifiers:"a",isARepeat:false,keyCode:0) else { return }
        if type == .keyDown { view.keyDown(with:event) } else { view.keyUp(with:event) }
      }
    }
    print("host-input-probe scheduled source=VZView")
    fflush(stdout)
  }
  func fail(_ error: Error) {
    fputs("Macaros VM: \(error)\n", stderr)
    exit(1)
  }
  func finishTermination() {
    guard !allowTermination else { return }
    shutdownPoll?.cancel();shutdownPoll=nil;allowTermination=true
    NSApp.terminate(nil)
  }
  func stopMachine() {
    guard !stopping else { return };stopping=true
    machine.stop { error in
      if let error {
        self.stopping=false;self.shutdownPoll?.cancel();self.shutdownPoll=nil
        fputs("VM stop failed; guest remains quiesced: \(error)\n",stderr)
        self.terminationPending=false
      } else { self.finishTermination() }
    }
  }
  func applicationShouldTerminate(_ sender:NSApplication)->NSApplication.TerminateReply {
    if allowTermination { return .terminateNow }
    if terminationPending { return .terminateCancel }
    terminationPending=true
    if let services=hostServices, !launchOptions.contains("--force-stop") {
      services.requestShutdown()
      print("[HOST] guest shutdown requested");fflush(stdout)
      let timer=DispatchSource.makeTimerSource(queue:.main)
      shutdownPoll=timer
      let start=ProcessInfo.processInfo.systemUptime
      var reported=false
      timer.schedule(deadline:.now(),repeating:0.1)
      timer.setEventHandler { [weak self] in
        guard let self else { return }
        if let result=services.shutdownStatus() {
          self.shutdownPoll?.cancel();self.shutdownPoll=nil
          if result==0 {
            print("[HOST] guest volume quiesced PASS");fflush(stdout);self.stopMachine()
          } else {
            fputs("Guest volume flush failed; VM kept running\n",stderr)
            self.terminationPending=false
          }
        } else if !reported && ProcessInfo.processInfo.systemUptime-start>=15 {
          reported=true
          fputs("Waiting for guest volume flush; VM kept running\n",stderr)
        }
      }
      timer.resume()
    } else { stopMachine() }
    // Keep the normal event loop serving VZ connections during the handshake.
    // finishTermination reissues terminate only after the machine has stopped.
    return .terminateCancel
  }
  func windowShouldClose(_ sender:NSWindow)->Bool {
    NSApp.terminate(nil);return false
  }
  func guestDidStop(_ virtualMachine: VZVirtualMachine) { finishTermination() }
  func virtualMachine(_ virtualMachine: VZVirtualMachine, didStopWithError error: Error) {
    fail(error)
  }
  func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}
let options = launchOptions
if options.contains("--input-probe") && options.contains("--headless") && (!options.contains("--gpu-window") || !options.contains("--usb-input")) {
  fputs("--input-probe requires a VZ window and USB input\n",stderr);exit(2)
}
if (!bundledLaunch && CommandLine.arguments.count < 3) || suppliedOptions.count != Set(suppliedOptions).count
  || options.contains(where: { $0 != "--headless" && $0 != "--gpu" && $0 != "--gpu-window" && $0 != "--usb-input" && $0 != "--input-probe" && $0 != "--network" && $0 != "--vsock-probe" && $0 != "--host-services" && $0 != "--clipboard-test" && $0 != "--force-stop" }) {
  fputs("usage: macaros-vm DISK_IMAGE STATE_DIRECTORY [--headless] [--gpu] [--gpu-window] [--usb-input] [--input-probe] [--network] [--vsock-probe] [--host-services] [--clipboard-test] [--force-stop]\n", stderr)
  exit(2)
}
if #available(macOS 13.0, *) {
  let app = NSApplication.shared
  app.setActivationPolicy(launchOptions.contains("--headless") && !launchOptions.contains("--gpu-window") ? .prohibited : .regular)
  do {
    let storage:VMStorage
    if bundledLaunch {
      guard let seed=Bundle.main.url(forResource:"System",withExtension:"img") else {
        throw NSError(domain:"MacarosVM",code:1,userInfo:[NSLocalizedDescriptionKey:"No bundled system image; supply DISK_IMAGE and STATE_DIRECTORY for command-line use."])
      }
      let state:URL
      if let override=ProcessInfo.processInfo.environment["MACAROS_VM_STATE_DIRECTORY"] {
        state=URL(fileURLWithPath:override,isDirectory:true)
      } else {
        state=try FileManager.default.url(for:.applicationSupportDirectory,in:.userDomainMask,appropriateFor:nil,create:true)
          .appendingPathComponent("Macaros VM/Default",isDirectory:true)
      }
      storage=try VMStorage(state:state,seed:seed)
    } else {
      storage=try VMStorage(state:URL(fileURLWithPath:CommandLine.arguments[2]),disk:URL(fileURLWithPath:CommandLine.arguments[1]))
    }
    let delegate=Application(storage:storage)
    app.delegate=delegate
    let menu=NSMenu();let item=NSMenuItem();menu.addItem(item)
    let applicationMenu=NSMenu();item.submenu=applicationMenu
    applicationMenu.addItem(withTitle:"Quit Macaros VM",action:#selector(NSApplication.terminate(_:)),keyEquivalent:"q")
    app.mainMenu=menu
    app.run()
    withExtendedLifetime(delegate) {}
  } catch {
    fputs("Macaros VM: \(error.localizedDescription)\n",stderr)
    if bundledLaunch && !options.contains("--headless") {
      let alert=NSAlert();alert.messageText="Macaros VM could not start"
      alert.informativeText=error.localizedDescription;alert.runModal()
    }
    exit(1)
  }
} else {
  fatalError("macOS 13 or later required")
}
