//
//  PARMCOLink.swift
//  Bluetooth Low Energy link to the PARMCO Raspberry Pi 4 controller.
//
//  AI-generated (Claude).
//
//  How it works:
//   - Uses Apple's CoreBluetooth framework as a BLE "central".
//   - The RP4 advertises a service (UUID 6E400001-...) with an RX characteristic
//     (the phone writes newline-terminated text commands) and a TX characteristic
//     (the RP4 sends replies and 5 Hz telemetry as notifications).
//   - The identifier of the last connected RP4 is saved in UserDefaults, so the
//     app reconnects by itself on launch (the "remembered" connection).
//     "Forget" clears it and the app scans again (re-pair).
//   - Telemetry lines look like:
//       TEL:ir=20.1;enc=19.9;ang=90.0;eok=1;cal=1;run=1;dir=CW;rpm=20.0;...
//

import Foundation
import CoreBluetooth
import Combine

enum LinkState: String {
    case bluetoothOff = "Bluetooth off"
    case idle = "Disconnected"
    case scanning = "Scanning…"
    case connecting = "Connecting…"
    case connected = "Connected"
}

struct Telemetry {
    var irRPM: Double = 0
    var encRPM: Double = 0
    var angle: Double = 0
    var encoderOK = false
    var calibrated = false
    var running = false
    var clockwise = true
    var desiredRPM: Double = 20
    var maintain = false
    var unlock = false
    var activity = "IDLE"
    var coilsOn = false
    var hold = false
    var logging = false
    var logSeconds: Double = 0
    var logFile = ""
}

struct DiscoveredPi: Identifiable {
    let id: UUID
    let name: String
    let rssi: Int
    let peripheral: CBPeripheral
}

final class PARMCOLink: NSObject, ObservableObject {
    static let serviceUUID = CBUUID(string: "6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
    static let rxUUID      = CBUUID(string: "6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
    static let txUUID      = CBUUID(string: "6E400003-B5A3-F393-E0A9-E50E24DCCA9E")
    private let savedKey = "PARMCO.savedPeripheral"

    @Published private(set) var state: LinkState = .idle
    @Published private(set) var telemetry = Telemetry()
    @Published private(set) var discovered: [DiscoveredPi] = []
    @Published private(set) var console: [String] = []
    @Published private(set) var lastError: String?
    @Published private(set) var lastEvent: String?
    @Published private(set) var rememberedName: String?

    private var central: CBCentralManager?
    /// Demo mode: no Bluetooth, fake "connected" state with sample readings.
    /// Used by the Xcode canvas preview and the iOS Simulator only.
    let isDemo: Bool
    private var pi: CBPeripheral?
    private var rx: CBCharacteristic?
    private var rxBuffer = ""
    private var userDisconnected = false

    init(demo: Bool = false) {
        isDemo = demo
        super.init()
        if demo {
            loadDemoState()
        } else {
            central = CBCentralManager(delegate: self, queue: .main)
        }
    }

    var isConnected: Bool { state == .connected }

    // MARK: - Public actions

    func startScan() {
        if isDemo { loadDemoState(); return }      // "reconnect" instantly in demo mode
        guard let central, central.state == .poweredOn else { return }
        discovered.removeAll()
        state = .scanning
        central.scanForPeripherals(withServices: [Self.serviceUUID], options: nil)
    }

    func connect(_ item: DiscoveredPi) {
        central?.stopScan()
        userDisconnected = false
        pi = item.peripheral
        pi?.delegate = self
        state = .connecting
        central?.connect(item.peripheral, options: nil)
    }

    func disconnect() {
        userDisconnected = true
        if isDemo { state = .idle; return }
        if let p = pi { central?.cancelPeripheralConnection(p) }
    }

    /// Forget the remembered RP4 (for the forget / re-pair demonstration).
    func forget() {
        disconnect()
        UserDefaults.standard.removeObject(forKey: savedKey)
        rememberedName = nil
        log("Forgot remembered PARMCO")
        startScan()
    }

    func send(_ command: String) {
        if isDemo {
            demoHandle(command)
            return
        }
        guard let p = pi, let c = rx, state == .connected else {
            lastError = "Not connected"
            return
        }
        let data = Data((command + "\n").utf8)
        let type: CBCharacteristicWriteType =
            c.properties.contains(.write) ? .withResponse : .withoutResponse
        p.writeValue(data, for: c, type: type)
        log("→ \(command)")
    }

    func clearError() { lastError = nil }

    /// Update the displayed state immediately; the next telemetry confirms it.
    func optimistic(_ change: (inout Telemetry) -> Void) { change(&telemetry) }

    func reportLocal(error: String) { lastError = error }

    // MARK: - Demo mode (preview / Simulator only)

    private func loadDemoState() {
        state = .connected
        rememberedName = "PARMCO (demo)"
        var t = Telemetry()
        t.irRPM = 19.8
        t.encRPM = 20.1
        t.angle = 90.0
        t.encoderOK = true
        t.calibrated = true
        t.desiredRPM = 20
        t.activity = "IDLE"
        telemetry = t
        console = ["Demo mode: no Bluetooth, sample readings",
                   "← PARMCO:CONNECTED",
                   "← ACK:CALIBRATE_ZERO",
                   "← EVT:MOVE_DONE:target=90.00;angle=90.02;err=-0.02"]
        lastEvent = "EVT:MOVE_DONE:target=90.00;angle=90.02;err=-0.02"
    }

    /// Rough local imitation of the RP4's replies so the UI can be clicked through.
    private func demoHandle(_ command: String) {
        log("→ \(command)")
        var t = telemetry
        switch command {
        case "START":
            t.running = true; t.coilsOn = true; t.activity = "RUN"
            t.irRPM = t.desiredRPM; t.encRPM = t.desiredRPM
        case "STOP":
            t.running = false; t.coilsOn = false; t.activity = "IDLE"
            t.irRPM = 0; t.encRPM = 0; t.unlock = false
        case "DIR:CW": t.clockwise = true
        case "DIR:CCW": t.clockwise = false
        case "FASTER": t.desiredRPM = min(t.unlock ? 400 : 100, t.desiredRPM * 1.05)
        case "SLOWER": t.desiredRPM = max(10, t.desiredRPM / 1.05)
        case "MODE:MAINTAIN": t.maintain = true
        case "MODE:MANUAL": t.maintain = false
        case "UNLOCK:ON": t.unlock = true
        case "UNLOCK:OFF": t.unlock = false
        case "HOLD:ON": t.hold = true
        case "HOLD:OFF": t.hold = false
        case "CALIBRATE_ZERO": t.calibrated = true; t.angle = 0
        case "SET_ZERO": t.angle = 0
        case "LOG:STOP": t.logging = false; t.logFile = ""; t.logSeconds = 0
        default:
            if command.hasPrefix("RPM:"), let v = Double(command.dropFirst(4)) { t.desiredRPM = v }
            if command.hasPrefix("ANGLE:"), let v = Double(command.dropFirst(6)) { t.angle = v }
            if command.hasPrefix("LOG:START") {
                t.logging = true; t.logSeconds = 12; t.logFile = "log_2026-11-11_13-30-00.csv"
            }
        }
        telemetry = t
        log("← ACK (demo)")
    }

    // MARK: - Incoming lines

    private func handle(line: String) {
        if line.hasPrefix("TEL:") {
            parseTelemetry(String(line.dropFirst(4)))
            return
        }
        log("← \(line)")
        if line.hasPrefix("ERR:") {
            lastError = Self.describe(error: line)
        } else if line.hasPrefix("EVT:") || line.hasPrefix("LOG:") {
            lastEvent = line
        }
    }

    private func parseTelemetry(_ body: String) {
        var t = telemetry
        for pair in body.split(separator: ";") {
            let kv = pair.split(separator: "=", maxSplits: 1).map(String.init)
            guard kv.count == 2 else { continue }
            let v = kv[1]
            switch kv[0] {
            case "ir":     t.irRPM = Double(v) ?? 0
            case "enc":    t.encRPM = Double(v) ?? 0
            case "ang":    t.angle = Double(v) ?? 0
            case "eok":    t.encoderOK = v == "1"
            case "cal":    t.calibrated = v == "1"
            case "run":    t.running = v == "1"
            case "dir":    t.clockwise = v == "CW"
            case "rpm":    t.desiredRPM = Double(v) ?? t.desiredRPM
            case "mode":   t.maintain = v == "MAINTAIN"
            case "unlock": t.unlock = v == "1"
            case "act":    t.activity = v
            case "en":     t.coilsOn = v == "1"
            case "hold":   t.hold = v == "1"
            case "log":    t.logging = v == "1"
            case "logt":   t.logSeconds = Double(v) ?? 0
            case "file":   t.logFile = v
            default: break
            }
        }
        telemetry = t
    }

    static func describe(error line: String) -> String {
        if line.hasPrefix("ERR:INVALID_RPM") { return "Invalid RPM — rejected, speed unchanged" }
        if line.hasPrefix("ERR:NOT_CALIBRATED") { return "Not calibrated — press Calibrate Zero first" }
        if line.hasPrefix("ERR:ENCODER") { return "Encoder not available" }
        if line.hasPrefix("ERR:STOP_FIRST") { return "Stop the motor first" }
        if line.hasPrefix("ERR:INVALID_ANGLE") { return "Invalid angle" }
        return line
    }

    private func log(_ s: String) {
        console.append(s)
        if console.count > 60 { console.removeFirst(console.count - 60) }
    }

    private func resetToDefaults() {
        telemetry = Telemetry()          // Manual, CW, 20 RPM, stopped, not calibrated
        lastError = nil
        lastEvent = nil
    }
}

// MARK: - CBCentralManagerDelegate

extension PARMCOLink: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        guard central.state == .poweredOn else {
            state = .bluetoothOff
            return
        }
        state = .idle
        // Remembered connection: reconnect to the saved RP4 without scanning.
        if let s = UserDefaults.standard.string(forKey: savedKey), let id = UUID(uuidString: s),
           let p = central.retrievePeripherals(withIdentifiers: [id]).first {
            rememberedName = p.name ?? "PARMCO"
            pi = p
            p.delegate = self
            state = .connecting
            central.connect(p, options: nil)
            log("Reconnecting to remembered \(rememberedName ?? "PARMCO")")
        } else {
            startScan()
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String)
            ?? peripheral.name ?? "PARMCO"
        if let i = discovered.firstIndex(where: { $0.id == peripheral.identifier }) {
            discovered[i] = DiscoveredPi(id: peripheral.identifier, name: name,
                                         rssi: RSSI.intValue, peripheral: peripheral)
        } else {
            discovered.append(DiscoveredPi(id: peripheral.identifier, name: name,
                                           rssi: RSSI.intValue, peripheral: peripheral))
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        UserDefaults.standard.set(peripheral.identifier.uuidString, forKey: savedKey)
        rememberedName = peripheral.name ?? "PARMCO"
        peripheral.discoverServices([Self.serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral,
                        error: Error?) {
        lastError = "Connection failed: \(error?.localizedDescription ?? "unknown")"
        state = .idle
        startScan()
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral,
                        error: Error?) {
        rx = nil
        rxBuffer = ""
        log("Disconnected")
        if userDisconnected {
            state = .idle
        } else {
            // Unexpected drop: keep a pending connection so it comes back by itself.
            state = .connecting
            central.connect(peripheral, options: nil)
        }
    }
}

// MARK: - CBPeripheralDelegate

extension PARMCOLink: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let svc = peripheral.services?.first(where: { $0.uuid == Self.serviceUUID }) else {
            lastError = "PARMCO service not found"
            return
        }
        peripheral.discoverCharacteristics([Self.rxUUID, Self.txUUID], for: svc)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService,
                    error: Error?) {
        for c in service.characteristics ?? [] {
            if c.uuid == Self.rxUUID { rx = c }
            if c.uuid == Self.txUUID { peripheral.setNotifyValue(true, for: c) }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic,
                    error: Error?) {
        guard characteristic.uuid == Self.txUUID, characteristic.isNotifying else { return }
        resetToDefaults()
        state = .connected
        log("Connected to \(peripheral.name ?? "PARMCO")")
        send("HELLO")
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic,
                    error: Error?) {
        guard characteristic.uuid == Self.txUUID, let data = characteristic.value,
              let chunk = String(data: data, encoding: .utf8) else { return }
        rxBuffer += chunk
        while let nl = rxBuffer.firstIndex(of: "\n") {
            let line = String(rxBuffer[..<nl]).trimmingCharacters(in: .whitespaces)
            rxBuffer = String(rxBuffer[rxBuffer.index(after: nl)...])
            if !line.isEmpty { handle(line: line) }
        }
        if rxBuffer.count > 1024 { rxBuffer = "" }
    }
}
