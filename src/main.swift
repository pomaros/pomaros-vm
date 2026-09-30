// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
import AppKit
import Virtualization

@available(macOS 13.0, *)
final class Application: NSObject, NSApplicationDelegate, VZVirtualMachineDelegate {
  var machine: VZVirtualMachine!
  var window: NSWindow!
  var machineView: VZVirtualMachineView?
  let serialInput = Pipe()
  let headless = CommandLine.arguments.contains("--headless")
  var terminationSignal: DispatchSourceSignal?
  var inputProbeTimer: DispatchSourceTimer?
  let disk: URL
  let state: URL
  init(disk: URL, state: URL) {
    self.disk = disk
    self.state = state
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
      if !headless || CommandLine.arguments.contains("--gpu") || CommandLine.arguments.contains("--gpu-window") {
        let graphics = VZVirtioGraphicsDeviceConfiguration()
        graphics.scanouts = [
          VZVirtioGraphicsScanoutConfiguration(widthInPixels: 1024, heightInPixels: 768)
        ]
        config.graphicsDevices = [graphics]
      }
      if !headless || CommandLine.arguments.contains("--usb-input") {
        config.keyboards = [VZUSBKeyboardConfiguration()]
        config.pointingDevices = [VZUSBScreenCoordinatePointingDeviceConfiguration()]
      }
      config.entropyDevices = [VZVirtioEntropyDeviceConfiguration()]
      try config.validate()
      machine = VZVirtualMachine(configuration: config)
      machine.delegate = self
      if !headless || CommandLine.arguments.contains("--gpu-window") {
        let view = VZVirtualMachineView()
        view.virtualMachine = machine
        machineView = view
        view.capturesSystemKeys = true
        window = NSWindow(
          contentRect: NSRect(x: 0, y: 0, width: 1024, height: 768),
          styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered,
          defer: false)
        window.title = "Macaros VM"
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
        self.machine.stop { _ in NSApp.terminate(nil) }
      }
      terminationSignal?.resume()
      machine.start { result in
        switch result {
        case .success:
          print("vm-start backend=Virtualization.framework result=PASS guest_boot=UNVERIFIED")
          fflush(stdout)
          if CommandLine.arguments.contains("--input-probe") {
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
  func guestDidStop(_ virtualMachine: VZVirtualMachine) { NSApp.terminate(nil) }
  func virtualMachine(_ virtualMachine: VZVirtualMachine, didStopWithError error: Error) {
    fail(error)
  }
  func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}
let options = Array(CommandLine.arguments.dropFirst(3))
if options.contains("--input-probe") && options.contains("--headless") && (!options.contains("--gpu-window") || !options.contains("--usb-input")) {
  fputs("--input-probe requires a VZ window and USB input\n",stderr);exit(2)
}
if CommandLine.arguments.count < 3 || options.count != Set(options).count
  || options.contains(where: { $0 != "--headless" && $0 != "--gpu" && $0 != "--gpu-window" && $0 != "--usb-input" && $0 != "--input-probe" }) {
  fputs("usage: macaros-vm DISK_IMAGE STATE_DIRECTORY [--headless] [--gpu] [--gpu-window] [--usb-input] [--input-probe]\n", stderr)
  exit(2)
}
if #available(macOS 13.0, *) {
  let app = NSApplication.shared
  app.setActivationPolicy(CommandLine.arguments.contains("--headless") && !CommandLine.arguments.contains("--gpu-window") ? .prohibited : .regular)
  let delegate = Application(
    disk: URL(fileURLWithPath: CommandLine.arguments[1]),
    state: URL(fileURLWithPath: CommandLine.arguments[2]))
  app.delegate = delegate
  app.run()
} else {
  fatalError("macOS 13 or later required")
}
