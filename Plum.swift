//
//  Plum.swift
//  Plum
//
//  Created by Jarrod Norwell on 27/8/2026.
//

import Foundation

@objc
public enum PlumButton : UInt32, CaseIterable, Codable {
    case up = 0x00,
         down = 0x01,
         left = 0x02,
         right = 0x03,
         a = 0x04,
         b = 0x05,
         c = 0x06,
         x = 0x07,
         y = 0x08,
         z = 0x09,
         start = 0x0A,
         mode = 0x0B
    case count = 0x0C
    
    public var uint32: UInt32 { rawValue }
}

public class PlumCommon {
    public init() {}
    
    public static var documentDirectoryURL: URL? {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first
    }
    
    public static var plumDirectoryURL: String? {
        if let documentDirectoryURL {
            documentDirectoryURL.appending(component: "Plum").path
        } else {
            nil
        }
    }
}

public actor PlumSystem {
    private var fileManager: FileManager = .default
    
    public init() {}
    
    public func printAbout() {
        plum.print_about()
    }
    
    
    public func initializePaths() {
        plum.initialize_paths()
    }
    
    public func initializeSystem() {
        plum.initialize_system()
    }
    
    
    public func destroySystem() {
        plum.destroy_system()
    }
    
    
    public func insertDisc(at url: URL) {
        plum.insert_disc(std.string(url.path))
    }
    
    
    public func set(change: Bool = false, isRunning: Bool = false) {
        if change {
            running = isRunning
        }
    }
    
    public var running: Bool {
        get {
            plum.is_running()
        }
        set {
            plum.is_running(true, newValue)
        }
    }
    
    public func set(change: Bool = false, isPaused: Bool = false) {
        if change {
            paused = isPaused
        }
    }
    
    public var paused: Bool {
        get {
            plum.is_paused()
        }
        set {
            plum.is_paused(true, newValue)
        }
    }
    
    public func start() {
        plum.start()
    }
    
    public func stop() {
        plum.stop()
    }
    
    
    public nonisolated func press(button: PlumButton, index: Int32) {
        plum.press_button(button.uint32, index)
    }
    
    public nonisolated func release(button: PlumButton, index: Int32) {
        plum.release_button(button.uint32, index)
    }
    
    
    public var framebufferHeight: Int32 {
        plum.framebuffer_height()
    }
    
    public var framebufferWidth: Int32 {
        plum.framebuffer_width()
    }
    
    
    public nonisolated func videoBuffer(callback: plum.VideoBufferCallback) {
        plum.video_buffer_callback(callback)
    }
    
    
    public func setContext(context: UnsafeMutableRawPointer) {
        plum.set_context(context)
    }
    
    public nonisolated func boxartURLString(for url: URL) -> String? {
        var title: String = url.deletingPathExtension().lastPathComponent
        title = title.replacingOccurrences(of: "&", with: "_")
        
        let repository: String = "https://raw.githubusercontent.com/libretro/libretro-thumbnails"
        let path: String = "Sega - Mega Drive - Genesis/Named_Boxarts"
        
        return "\(repository)/refs/heads/master/\(path)/\(title).png"
    }
}
