#!/usr/bin/swift
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
// Capture only a large, normal-layer window owned by the supplied test PID.
import Foundation
import CoreGraphics

guard CommandLine.arguments.count == 3,
      let pid = Int(CommandLine.arguments[1]), pid > 0 else {
    fputs("usage: capture-window.swift PID OUTPUT.png\n", stderr)
    exit(2)
}
let windows = CGWindowListCopyWindowInfo([.optionOnScreenOnly, .excludeDesktopElements], kCGNullWindowID) as? [[String: Any]] ?? []
let owned = windows.filter { window in
    guard (window[kCGWindowOwnerPID as String] as? Int) == pid,
          (window[kCGWindowLayer as String] as? Int) == 0,
          let bounds = window[kCGWindowBounds as String] as? [String: Any],
          let width = bounds["Width"] as? Double,
          let height = bounds["Height"] as? Double else { return false }
    return width >= 640 && height >= 480
}
guard owned.count == 1, let identifier = owned[0][kCGWindowNumber as String] as? Int else {
    fputs("expected exactly one large visible window for owned PID\n", stderr)
    exit(1)
}
let capture = Process()
capture.executableURL = URL(fileURLWithPath: "/usr/sbin/screencapture")
capture.arguments = ["-x", "-l", String(identifier), CommandLine.arguments[2]]
try capture.run()
capture.waitUntilExit()
guard capture.terminationStatus == 0 else { exit(capture.terminationStatus) }
print("window-capture result=PASS pid=\(pid) window=\(identifier)")
