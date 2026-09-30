// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
import AppKit
import Virtualization

@available(macOS 13.0, *)
final class Application: NSObject, NSApplicationDelegate, VZVirtualMachineDelegate {
  var machine: VZVirtualMachine!
  var window: NSWindow!
  let serialInput = Pipe()
  let headless = CommandLine.arguments.contains("--headless")
  var terminationSignal: DispatchSourceSignal?
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
      if !headless {
        let graphics = VZVirtioGraphicsDeviceConfiguration()
        graphics.scanouts = [
          VZVirtioGraphicsScanoutConfiguration(widthInPixels: 1024, heightInPixels: 768)
        ]
        config.graphicsDevices = [graphics]
        config.keyboards = [VZUSBKeyboardConfiguration()]
        config.pointingDevices = [VZUSBScreenCoordinatePointingDeviceConfiguration()]
      }
      config.entropyDevices = [VZVirtioEntropyDeviceConfiguration()]
      try config.validate()
      machine = VZVirtualMachine(configuration: config)
      machine.delegate = self
      if !headless {
        let view = VZVirtualMachineView()
        view.virtualMachine = machine
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
        case .failure(let error): self.fail(error)
        }
      }
    } catch { fail(error) }
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
if !(CommandLine.arguments.count == 3
  || (CommandLine.arguments.count == 4 && CommandLine.arguments[3] == "--headless"))
{
  fputs("usage: macaros-vm DISK_IMAGE STATE_DIRECTORY [--headless]\n", stderr)
  exit(2)
}
if #available(macOS 13.0, *) {
  let app = NSApplication.shared
  app.setActivationPolicy(CommandLine.arguments.contains("--headless") ? .prohibited : .regular)
  let delegate = Application(
    disk: URL(fileURLWithPath: CommandLine.arguments[1]),
    state: URL(fileURLWithPath: CommandLine.arguments[2]))
  app.delegate = delegate
  app.run()
} else {
  fatalError("macOS 13 or later required")
}
