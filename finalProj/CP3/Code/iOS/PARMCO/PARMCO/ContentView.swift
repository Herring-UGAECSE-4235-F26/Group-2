//
//  ContentView.swift
//  PARMCO iPhone controller UI.
//
//  AI-generated (Claude).
//
//  Required controls (project spec, "Control from the phone"):
//   direction, faster/slower, start/stop, desired RPM 10-100 with visible
//   rejection, Manual/Maintain selector, status display (IR RPM, encoder RPM,
//   encoder angle, calibration status), calibrate zero, set to zero, angle
//   target, start/stop logging with indicator (file name + elapsed time),
//   over-range unlock. Extras: mark button, live angle dial, feedback-hold
//   toggle, message console, haptics.
//

import SwiftUI
import UIKit

struct ContentView: View {
    @EnvironmentObject var link: PARMCOLink

    @State private var rpmText = "20"
    @State private var rpmInvalid = false
    @State private var angleText = "90"
    @State private var angleInvalid = false
    @State private var logLabel = ""
    @State private var localMessage: String?
    @State private var lastAngleTarget: Double?
    @FocusState private var focused: Bool

    private var t: Telemetry { link.telemetry }
    private var rpmMax: Double { t.unlock ? 400 : 100 }

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 14) {
                    connectionCard
                    if link.isConnected {
                        statusCard
                        motorCard
                        positionCard
                        loggingCard
                        advancedCard
                    }
                    consoleCard
                }
                .padding(.horizontal)
                .padding(.bottom, 30)
            }
            .background(Color(.systemGroupedBackground))
            .navigationTitle("PARMCO")
            .toolbar {
                ToolbarItemGroup(placement: .keyboard) {
                    Spacer()
                    Button("Done") { focused = false }
                }
            }
            .onChange(of: link.lastError) { _, err in
                guard let err else { return }
                if err.hasPrefix("Invalid RPM") { rpmInvalid = true }
                if err.hasPrefix("Invalid angle") { angleInvalid = true }
            }
            .onChange(of: link.state) { _, s in
                if s == .connected {        // defaults on connect
                    rpmText = "20"
                    rpmInvalid = false
                    angleInvalid = false
                    localMessage = nil
                    lastAngleTarget = nil
                }
            }
        }
    }

    // MARK: - Connection

    private var connectionCard: some View {
        Card {
            HStack {
                Circle()
                    .fill(link.isConnected ? Color.green : (link.state == .connecting ? .orange : .gray))
                    .frame(width: 12, height: 12)
                VStack(alignment: .leading, spacing: 2) {
                    Text(link.state.rawValue).font(.headline)
                    if let n = link.rememberedName {
                        Text("Remembered: \(n)").font(.caption).foregroundStyle(.secondary)
                    }
                }
                Spacer()
                if link.isConnected || link.state == .connecting {
                    Button("Disconnect") { tap(); link.disconnect() }
                        .buttonStyle(.bordered)
                } else {
                    Button("Scan") { tap(); link.startScan() }
                        .buttonStyle(.borderedProminent)
                }
            }
            if !link.isConnected && !link.discovered.isEmpty {
                Divider()
                ForEach(link.discovered) { d in
                    Button {
                        tap(); link.connect(d)
                    } label: {
                        HStack {
                            Image(systemName: "dot.radiowaves.left.and.right")
                            Text(d.name)
                            Spacer()
                            Text("\(d.rssi) dBm").font(.caption).foregroundStyle(.secondary)
                            Text("Connect").bold()
                        }
                    }
                }
            }
            if link.rememberedName != nil {
                Button(role: .destructive) { tap(); link.forget() } label: {
                    Label("Forget this PARMCO", systemImage: "xmark.circle")
                }
                .font(.footnote)
            }
        }
    }

    // MARK: - Status

    private var statusCard: some View {
        Card(title: "Status") {
            HStack(alignment: .center, spacing: 16) {
                AngleDial(angle: t.angle, target: lastAngleTarget, calibrated: t.calibrated)
                    .frame(width: 128, height: 128)
                VStack(alignment: .leading, spacing: 8) {
                    Metric(label: "Actual RPM (IR)", value: String(format: "%.1f", t.irRPM), big: true)
                    Metric(label: "Encoder RPM", value: String(format: "%.1f", t.encRPM))
                    Metric(label: "Encoder angle",
                           value: t.encoderOK ? String(format: "%.1f°", t.angle) : "—")
                }
                Spacer(minLength: 0)
            }
            HStack(spacing: 8) {
                Badge(text: t.calibrated ? "Calibrated" : "Not calibrated",
                      color: t.calibrated ? .green : .red)
                Badge(text: t.running ? "Running" : (t.activity == "MOVE" ? "Moving" : "Stopped"),
                      color: t.running || t.activity == "MOVE" ? .blue : .gray)
                Badge(text: t.coilsOn ? "Coils ON" : "Coils off", color: t.coilsOn ? .orange : .gray)
                if t.activity == "HOLD" { Badge(text: "Holding", color: .purple) }
            }
            HStack {
                Text("Set: \(String(format: "%.1f", t.desiredRPM)) RPM · \(t.clockwise ? "CW" : "CCW") · \(t.maintain ? "Maintain" : "Manual")")
                    .font(.footnote).foregroundStyle(.secondary)
                Spacer()
            }
            if let msg = localMessage ?? link.lastError {
                HStack {
                    Image(systemName: "exclamationmark.triangle.fill")
                    Text(msg)
                    Spacer()
                    Button { localMessage = nil; link.clearError() } label: { Image(systemName: "xmark") }
                }
                .font(.footnote.weight(.semibold))
                .foregroundStyle(.white)
                .padding(10)
                .background(Color.red, in: RoundedRectangle(cornerRadius: 10))
            }
        }
    }

    // MARK: - Motor

    private var motorCard: some View {
        Card(title: "Motor") {
            HStack(spacing: 12) {
                Button { tap(); link.send("START") } label: {
                    Label("Start", systemImage: "play.fill").frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent).tint(.green).controlSize(.large)
                Button { tapHeavy(); link.send("STOP") } label: {
                    Label("Stop", systemImage: "stop.fill").frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent).tint(.red).controlSize(.large)
            }

            Picker("Direction", selection: Binding(
                get: { t.clockwise },
                set: { cw in tap(); link.optimistic { $0.clockwise = cw }; link.send(cw ? "DIR:CW" : "DIR:CCW") })) {
                Text("Clockwise").tag(true)
                Text("Counter-clockwise").tag(false)
            }
            .pickerStyle(.segmented)

            Picker("Mode", selection: Binding(
                get: { t.maintain },
                set: { m in tap(); link.optimistic { $0.maintain = m }; link.send(m ? "MODE:MAINTAIN" : "MODE:MANUAL") })) {
                Text("Manual").tag(false)
                Text("Maintain").tag(true)
            }
            .pickerStyle(.segmented)

            HStack(spacing: 12) {
                Button { tap(); link.send("SLOWER") } label: {
                    Label("Slower", systemImage: "tortoise.fill").frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                Button { tap(); link.send("FASTER") } label: {
                    Label("Faster", systemImage: "hare.fill").frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
            }

            VStack(alignment: .leading, spacing: 4) {
                Text("Desired RPM (\(Int(10))–\(Int(rpmMax)))").font(.subheadline)
                HStack {
                    TextField("RPM", text: $rpmText)
                        .keyboardType(.decimalPad)
                        .focused($focused)
                        .padding(8)
                        .background(RoundedRectangle(cornerRadius: 8)
                            .stroke(rpmInvalid ? Color.red : Color.secondary.opacity(0.4),
                                    lineWidth: rpmInvalid ? 2 : 1))
                        .onChange(of: rpmText) { _, _ in rpmInvalid = false }
                    Button("Set") { submitRPM() }
                        .buttonStyle(.borderedProminent)
                }
                if rpmInvalid {
                    Text("Invalid — enter 10 to \(Int(rpmMax)) RPM. Speed unchanged.")
                        .font(.caption.bold()).foregroundStyle(.red)
                }
            }
        }
    }

    private func submitRPM() {
        focused = false
        let v = Double(rpmText.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: ",", with: "."))
        guard let r = v, r >= 10, r <= rpmMax else {
            rpmInvalid = true
            tapWarning()
            return           // rejected: nothing sent, motor keeps its setting
        }
        rpmInvalid = false
        tap()
        link.send(String(format: "RPM:%.1f", r))
    }

    // MARK: - Position

    private var positionCard: some View {
        Card(title: "Position") {
            Text("Align the Sharpie blade with 0° on the protractor, then Calibrate Zero.")
                .font(.caption).foregroundStyle(.secondary)
            HStack(spacing: 12) {
                Button { tap(); localMessage = nil; link.send("CALIBRATE_ZERO") } label: {
                    Label("Calibrate Zero", systemImage: "scope").frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent).tint(.indigo)
                Button { setToZero() } label: {
                    Label("Set to Zero", systemImage: "arrow.uturn.backward.circle").frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
                .opacity(t.calibrated ? 1 : 0.55)
            }
            VStack(alignment: .leading, spacing: 4) {
                Text("Angle target (0–359.9°) at the desired RPM").font(.subheadline)
                HStack {
                    TextField("Angle", text: $angleText)
                        .keyboardType(.decimalPad)
                        .focused($focused)
                        .padding(8)
                        .background(RoundedRectangle(cornerRadius: 8)
                            .stroke(angleInvalid ? Color.red : Color.secondary.opacity(0.4),
                                    lineWidth: angleInvalid ? 2 : 1))
                        .onChange(of: angleText) { _, _ in angleInvalid = false }
                    Button("Go") { submitAngle() }
                        .buttonStyle(.borderedProminent)
                        .opacity(t.calibrated ? 1 : 0.55)
                }
                if angleInvalid {
                    Text("Invalid — enter 0 to 359.9°").font(.caption.bold()).foregroundStyle(.red)
                }
            }
        }
    }

    private func setToZero() {
        guard t.calibrated else {
            localMessage = "Refused: not calibrated — press Calibrate Zero first"
            tapWarning()
            return
        }
        tap()
        lastAngleTarget = 0
        link.send("SET_ZERO")
    }

    private func submitAngle() {
        focused = false
        guard let a = Double(angleText.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: ",", with: ".")),
              a >= 0, a < 360 else {
            angleInvalid = true
            tapWarning()
            return
        }
        guard t.calibrated else {
            localMessage = "Refused: not calibrated — press Calibrate Zero first"
            tapWarning()
            return
        }
        tap()
        lastAngleTarget = a
        link.send(String(format: "ANGLE:%.1f", a))
    }

    // MARK: - Logging

    private var loggingCard: some View {
        Card(title: "Data logging") {
            HStack {
                Circle().fill(t.logging ? Color.red : Color.gray.opacity(0.4)).frame(width: 14, height: 14)
                    .opacity(t.logging ? (Int(t.logSeconds) % 2 == 0 ? 1 : 0.4) : 1)
                Text(t.logging ? "LOGGING ON" : "Logging off").font(.headline)
                    .foregroundStyle(t.logging ? .red : .secondary)
                Spacer()
                if t.logging {
                    Text(elapsed(t.logSeconds)).font(.system(.title3, design: .monospaced))
                }
            }
            if t.logging {
                Text(t.logFile).font(.caption.monospaced()).foregroundStyle(.secondary)
                    .lineLimit(1).truncationMode(.middle)
            } else {
                TextField("Optional test label (e.g. testC_137deg)", text: $logLabel)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .focused($focused)
                    .font(.footnote)
            }
            HStack(spacing: 12) {
                Button {
                    tap()
                    if t.logging {
                        link.send("LOG:STOP")
                    } else {
                        let label = logLabel.filter { $0.isLetter || $0.isNumber || $0 == "_" || $0 == "-" }
                        link.send(label.isEmpty ? "LOG:START" : "LOG:START:\(label)")
                    }
                } label: {
                    Label(t.logging ? "Stop logging" : "Start logging",
                          systemImage: t.logging ? "stop.circle" : "record.circle")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent).tint(t.logging ? .gray : .red)
                Button { tap(); link.send("MARK") } label: {
                    Label("Mark", systemImage: "flag.fill")
                }
                .buttonStyle(.bordered)
                .disabled(!t.logging)
            }
        }
    }

    // MARK: - Advanced

    private var advancedCard: some View {
        Card(title: "Test controls") {
            Toggle(isOn: Binding(
                get: { t.unlock },
                set: { on in tapWarning(); link.optimistic { $0.unlock = on }; link.send(on ? "UNLOCK:ON" : "UNLOCK:OFF") })) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Over-range unlock").bold()
                    Text("Allows > 100 RPM for tests B and G only. Turns off automatically when the motor stops. Eye protection!")
                        .font(.caption).foregroundStyle(.secondary)
                }
            }
            .tint(.orange)
            Toggle(isOn: Binding(
                get: { t.hold },
                set: { on in tap(); link.optimistic { $0.hold = on }; link.send(on ? "HOLD:ON" : "HOLD:OFF") })) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("Feedback hold").bold()
                    Text("After a position move: coils off, encoder corrects if the blade leaves ±1°.")
                        .font(.caption).foregroundStyle(.secondary)
                }
            }
        }
    }

    // MARK: - Console

    private var consoleCard: some View {
        Card(title: "Messages") {
            if let e = link.lastEvent {
                Text(e).font(.caption.monospaced()).foregroundStyle(.blue)
            }
            ScrollView {
                VStack(alignment: .leading, spacing: 2) {
                    ForEach(Array(link.console.suffix(25).enumerated()), id: \.offset) { _, line in
                        Text(line).font(.caption2.monospaced()).frame(maxWidth: .infinity, alignment: .leading)
                    }
                }
            }
            .frame(height: 120)
        }
    }

    // MARK: - Helpers

    private func elapsed(_ s: Double) -> String {
        let i = Int(s)
        return String(format: "%02d:%02d", i / 60, i % 60)
    }

    private func tap() { UIImpactFeedbackGenerator(style: .light).impactOccurred() }
    private func tapHeavy() { UIImpactFeedbackGenerator(style: .heavy).impactOccurred() }
    private func tapWarning() { UINotificationFeedbackGenerator().notificationOccurred(.warning) }
}

// MARK: - Small views

struct Card<Content: View>: View {
    var title: String? = nil
    @ViewBuilder var content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            if let title {
                Text(title.uppercased()).font(.caption.bold()).foregroundStyle(.secondary)
            }
            content
        }
        .padding()
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Color(.secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 16))
    }
}

struct Metric: View {
    let label: String
    let value: String
    var big = false

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text(label).font(.caption).foregroundStyle(.secondary)
            Text(value)
                .font(big ? .system(size: 30, weight: .bold, design: .rounded)
                          : .system(.title3, design: .rounded).weight(.semibold))
                .monospacedDigit()
        }
    }
}

struct Badge: View {
    let text: String
    let color: Color

    var body: some View {
        Text(text)
            .font(.caption.bold())
            .padding(.horizontal, 8).padding(.vertical, 4)
            .background(color.opacity(0.18), in: Capsule())
            .foregroundStyle(color)
    }
}

/// Live dial: needle = encoder angle (0° at top), dot = last angle target.
struct AngleDial: View {
    let angle: Double
    let target: Double?
    let calibrated: Bool

    var body: some View {
        Canvas { ctx, size in
            let r = min(size.width, size.height) / 2 - 4
            let c = CGPoint(x: size.width / 2, y: size.height / 2)
            func point(_ deg: Double, _ radius: CGFloat) -> CGPoint {
                let a = (deg - 90) * .pi / 180
                return CGPoint(x: c.x + radius * cos(a), y: c.y + radius * sin(a))
            }
            ctx.stroke(Path(ellipseIn: CGRect(x: c.x - r, y: c.y - r, width: 2 * r, height: 2 * r)),
                       with: .color(.secondary.opacity(0.5)), lineWidth: 2)
            for d in stride(from: 0.0, to: 360.0, by: 30.0) {
                var p = Path()
                p.move(to: point(d, r))
                p.addLine(to: point(d, r - (d.truncatingRemainder(dividingBy: 90) == 0 ? 12 : 6)))
                ctx.stroke(p, with: .color(.secondary), lineWidth: 1.5)
            }
            if let target {
                let tp = point(target, r - 4)
                ctx.fill(Path(ellipseIn: CGRect(x: tp.x - 5, y: tp.y - 5, width: 10, height: 10)),
                         with: .color(.orange))
            }
            var blade = Path()
            blade.move(to: point(angle + 180, r * 0.45))
            blade.addLine(to: point(angle, r * 0.85))
            ctx.stroke(blade, with: .color(calibrated ? .blue : .gray),
                       style: StrokeStyle(lineWidth: 5, lineCap: .round))
            ctx.fill(Path(ellipseIn: CGRect(x: c.x - 5, y: c.y - 5, width: 10, height: 10)),
                     with: .color(.primary))
        }
        .overlay(alignment: .top) { Text("0°").font(.caption2).offset(y: 14) }
    }
}
