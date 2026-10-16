// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper

import Cocoa
import ScreenCaptureKit

private enum WindowCaptureError: LocalizedError {
    case unsupportedSystem
    case windowNotOwned(CGWindowID)
    case invalidDimensions
    case pngEncodingFailed

    var errorDescription: String? {
        switch self {
        case .unsupportedSystem:
            return "Capturing the application's own window requires macOS 14.4 or later."
        case .windowNotOwned(let windowID):
            return "Window \(windowID) is not available for capture by this application."
        case .invalidDimensions:
            return "The requested window has invalid capture dimensions."
        case .pngEncodingFailed:
            return "The captured window could not be encoded as PNG."
        }
    }
}

/// Capture the requested window belonging to this process, including its title bar.
/// ScreenCaptureKit's currentProcess inventory does not request global screen access.
@MainActor
func captureOwnedWindow(windowID: CGWindowID, to destination: URL) async throws {
    guard #available(macOS 14.4, *) else {
        throw WindowCaptureError.unsupportedSystem
    }

    let content = try await SCShareableContent.currentProcess
    let pid = ProcessInfo.processInfo.processIdentifier
    guard let window = content.windows.first(where: {
        $0.windowID == windowID && $0.owningApplication?.processID == pid
    }) else {
        throw WindowCaptureError.windowNotOwned(windowID)
    }

    let filter = SCContentFilter(desktopIndependentWindow: window)
    let scale = Double(filter.pointPixelScale)
    let width = ceil(Double(window.frame.width) * scale)
    let height = ceil(Double(window.frame.height) * scale)
    guard width.isFinite, height.isFinite, width > 0, height > 0,
          width < Double(Int.max), height < Double(Int.max) else {
        throw WindowCaptureError.invalidDimensions
    }

    let configuration = SCStreamConfiguration()
    configuration.width = Int(width)
    configuration.height = Int(height)
    configuration.showsCursor = false
    configuration.ignoreShadowsSingleWindow = true

    let image = try await SCScreenshotManager.captureImage(
        contentFilter: filter, configuration: configuration)
    guard let png = NSBitmapImageRep(cgImage: image).representation(
        using: .png, properties: [:]) else {
        throw WindowCaptureError.pngEncodingFailed
    }
    try png.write(to: destination, options: .atomic)
}
