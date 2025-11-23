//
//  Plum.swift
//  Plum
//
//  Created by Jarrod Norwell on 8/11/2025.
//

@objc
public enum SGButton : UInt32 {
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
}

public enum SEGADeviceType: String, CustomStringConvertible {
    case masterSystemController = "O",
         megaDriveControlPad  = "J",
         keyboard = "K",
         serialIO = "R",
         printer = "P",
         tablet  = "T",
         trackball = "B",
         paddleControl = "V",
         analogJoystick = "A",
         mouse = "M",
         floppyDiskDrive = "F",
         cdrom = "C",
         multitap = "4",
         sixButtonControlPad = "6"
    
    public var description: String {
        switch self {
        case .masterSystemController: "Master System Controller"
        case .megaDriveControlPad: "Mega Drive Control Pad"
        case .keyboard: "Keyboard (Mega Drive Keyboard)"
        case .serialIO: "Serial I/O (RS232C)"
        case .printer: "Printer (4 Color Plotter Printer)"
        case .tablet: "Tablet (Sega Graphic Board)"
        case .trackball: "Trackball (Sports Pad)"
        case .paddleControl: "Paddle Control"
        case .analogJoystick: "Analog Joystick (XE-1 AP)"
        case .mouse: "Mouse (Sega Mouse, Mega Mouse)"
        case .floppyDiskDrive: "FDD (Mega Drive Floppy Disk Drive)"
        case .cdrom: "CD-ROM (Sega Mega-CD)"
        case .multitap: "Multi-tap (Team Player)"
        case .sixButtonControlPad: "Six Button Control Pad"
        }
    }
    
    public var string: String { description }
}

@objcMembers
public class CUE : NSObject {
    var url: URL
    init(_ url: URL) {
        self.url = url
    }
    
    public func firstTrackFilename() -> String? {
        do {
            let contents = try String(contentsOf: url, encoding: .utf8)

            for line in contents.components(separatedBy: .newlines) {
                let trimmed = line.trimmingCharacters(in: .whitespaces)

                guard trimmed.hasPrefix("FILE ") else { continue }

                // FILE "<name>" BINARY
                // Extract the quoted filename
                if let firstQuote = trimmed.firstIndex(of: "\""),
                   let lastQuote = trimmed.lastIndex(of: "\""),
                   firstQuote != lastQuote
                {
                    let filename = String(trimmed[trimmed.index(after: firstQuote)..<lastQuote])
                    return filename
                }
            }

            return nil
        } catch {
            return nil
        }
    }
}

public class Plum {
    public var emulator: PlumEmulator = .shared()
    
    public init() {}
    
    public func insert(_ cartridge: URL) -> [String] {
        emulator.insert(cartridge)
    }
    
    public func start() {
        emulator.start()
    }
    
    public func stop() {
        emulator.stop()
    }
    
    public var isPaused: Bool {
        get {
            emulator.isPaused()
        }
        set {
            emulator.pause(newValue)
        }
    }
    
    public func pause(_ pause: Bool) {
        emulator.pause(pause)
    }
    
    public func updateSettings() {
        emulator.updateSettings()
    }
    
    public func framebuffer(_ buffer: @escaping (UnsafeMutablePointer<UInt32>, _ width: Int, _ height: Int) -> Void) {
        emulator.framebuffer = buffer
    }
    
    public func input(_ slot: Int, _ button: SGButton, _ pressed: Bool) {
        emulator.input(slot, button: button.rawValue, pressed: pressed)
    }
}
