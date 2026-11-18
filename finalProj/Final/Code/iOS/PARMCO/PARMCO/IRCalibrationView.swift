//
//  IRCalibrationView.swift
//  Guided IR sensor calibration (set the sensor module's potentiometer).
//
//  AI-generated (Claude).
//
//  While this window is open the RP4 is in IR calibration mode: the motor and
//  the feedback hold are off, so the blade turns freely by hand, and START /
//  SET_ZERO / ANGLE are refused. The RP4 streams the raw sensor state 20 times
//  a second (IRCAL:blade=0|1;edges=n;rpm=x).
//
//  Step 1  Blades away from the sensor. The user turns the pot until the sensor
//          reads Clear, and it must stay clear (no detections) for 2 s.
//  Step 2  One blade directly in front of the sensor. Detection must hold for
//          1 s; the app then confirms it.
//  Done    Optional spin check: pulses counted and IR RPM while spinning by hand.
//

import SwiftUI
import UIKit

struct IRCalibrationView: View {
    @EnvironmentObject var link: PARMCOLink
    @Environment(\.dismiss) private var dismiss

    private enum Step: Int { case clear = 1, detect = 2, done = 3 }

    @State private var step: Step = .clear
    @State private var stableSince: Date?
    @State private var stableEdges = 0
    @State private var clearOK = false
    @State private var detectOK = false
    @State private var stepStarted = Date()
    @State private var edgeTimes: [Date] = []
    @State private var lastEdges = 0
    @State private var doneEdges = 0
    @State private var now = Date()

    private let clearHold: TimeInterval = 2.0
    private let detectHold: TimeInterval = 1.0
    private let tick = Timer.publish(every: 0.1, on: .main, in: .common).autoconnect()

    private var r: IRCalReading { link.irCal }

    /// More than 3 detections in 3 s while the blade should be still = flicker.
    private var flickering: Bool { edgeTimes.filter { now.timeIntervalSince($0) < 3 }.count > 3 }
    private var heldFor: TimeInterval { stableSince.map { now.timeIntervalSince($0) } ?? 0 }

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 16) {
                    motorOffBanner
                    progress
                    switch step {
                    case .clear:  clearStep
                    case .detect: detectStep
                    case .done:   doneStep
                    }
                }
                .padding()
            }
            .background(Color(.systemGroupedBackground))
            .navigationTitle("IR sensor calibration")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button(step == .done ? "Close" : "Cancel") { dismiss() }
                }
            }
        }
        .onAppear { link.startIRCalibration() }
        .onDisappear { link.stopIRCalibration() }
        .onReceive(tick) { _ in update() }
    }

    // MARK: - Steps

    private var clearStep: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Step 1 — No blade in front of the sensor").font(.headline)
            BladeSketch(bladeOnSensor: false)
            Text("Turn the propeller by hand so that **neither blade** is in front of the IR sensor.")
            Text("Then slowly turn the sensor's **potentiometer** until the reading below shows **Clear** and the LED on the sensor goes off. Stop just past the point where it switches.")
            SensorLight(reading: r, connected: link.isConnected)
            if flickering {
                Hint(icon: "waveform.path", color: .orange,
                     text: "The reading is flickering. Turn the potentiometer a little further toward less sensitive.")
            }
            if !r.blade && r.received && !clearOK {
                ProgressView(value: min(heldFor / clearHold, 1)) {
                    Text("Holding Clear… \(String(format: "%.1f", min(heldFor, clearHold))) / \(Int(clearHold)) s")
                        .font(.caption).foregroundStyle(.secondary)
                }
            }
            Button {
                UIImpactFeedbackGenerator(style: .light).impactOccurred()
                go(to: .detect)
            } label: {
                Label(clearOK ? "Confirm — sensor reads Clear" : "Waiting for a steady Clear reading",
                      systemImage: clearOK ? "checkmark.circle.fill" : "hourglass")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .tint(.green)
            .disabled(!clearOK)
        }
        .card()
    }

    private var detectStep: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Step 2 — Blade in front of the sensor").font(.headline)
            BladeSketch(bladeOnSensor: true)
            Text("Now turn the propeller so that **one blade is directly in front** of the IR sensor. Don't touch the potentiometer.")
            SensorLight(reading: r, connected: link.isConnected)
            if detectOK {
                Hint(icon: "checkmark.seal.fill", color: .green,
                     text: "Blade detected. The sensor sees the blade and ignores the empty space, so it is calibrated.")
                Button {
                    UIImpactFeedbackGenerator(style: .light).impactOccurred()
                    go(to: .done)
                } label: {
                    Label("Next", systemImage: "arrow.right.circle.fill").frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
            } else {
                if r.blade {
                    ProgressView(value: min(heldFor / detectHold, 1)) {
                        Text("Confirming detection…").font(.caption).foregroundStyle(.secondary)
                    }
                }
                if now.timeIntervalSince(stepStarted) > 8 && !r.blade {
                    Hint(icon: "exclamationmark.triangle.fill", color: .orange,
                         text: "Still not detecting the blade? The potentiometer was turned too far. Turn it back slowly until the blade is detected, then re-check step 1.")
                    Button("Re-check step 1") { go(to: .clear) }
                        .buttonStyle(.bordered)
                }
            }
        }
        .card()
    }

    private var doneStep: some View {
        VStack(alignment: .leading, spacing: 14) {
            Label("IR sensor calibrated", systemImage: "checkmark.seal.fill")
                .font(.title3.bold()).foregroundStyle(.green)
            VStack(alignment: .leading, spacing: 6) {
                Label("Clear with no blade in front", systemImage: "checkmark")
                Label("Detects a blade in front", systemImage: "checkmark")
            }
            .font(.subheadline)
            Divider()
            Text("Optional spin check").font(.headline)
            Text("Give the propeller a gentle spin by hand. Each turn should add **2 pulses** (one per blade), and the IR RPM should look steady while it spins.")
            HStack(spacing: 24) {
                Metric(label: "Pulses", value: "\(max(r.edges - doneEdges, 0))", big: true)
                Metric(label: "IR RPM", value: String(format: "%.1f", r.rpm), big: true)
            }
            SensorLight(reading: r, connected: link.isConnected)
            Button {
                dismiss()
            } label: {
                Label("Finish — motor stays off", systemImage: "checkmark.circle.fill").frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
        }
        .card()
    }

    // MARK: - Shared pieces

    private var motorOffBanner: some View {
        Hint(icon: "hand.raised.fill", color: .blue,
             text: "The motor is off while this window is open, so the propeller turns freely by hand. Start, Set to Zero and angle moves are locked out.")
    }

    private var progress: some View {
        HStack(spacing: 8) {
            ForEach([Step.clear, .detect, .done], id: \.rawValue) { s in
                Capsule()
                    .fill(s.rawValue <= step.rawValue ? Color.accentColor : Color(.systemGray4))
                    .frame(height: 6)
            }
        }
        .accessibilityLabel("Step \(min(step.rawValue, 2)) of 2")
    }

    // MARK: - Logic

    private func go(to s: Step) {
        step = s
        stableSince = nil
        stableEdges = r.edges
        stepStarted = Date()
        edgeTimes.removeAll()
        if s == .clear { clearOK = false; detectOK = false }
        if s == .done { doneEdges = r.edges }
    }

    private func update() {
        now = Date()
        if r.edges > lastEdges {
            edgeTimes.append(contentsOf: Array(repeating: now, count: min(r.edges - lastEdges, 10)))
            edgeTimes.removeAll { now.timeIntervalSince($0) > 5 }
        }
        lastEdges = r.edges
        guard r.received, link.isConnected else { stableSince = nil; return }

        switch step {
        case .clear:
            // Clear, and no new detection since the clear period began.
            if r.blade || r.edges != stableEdges {
                stableSince = nil
                stableEdges = r.edges
                clearOK = false
            } else if stableSince == nil {
                stableSince = now
            } else if heldFor >= clearHold && !clearOK {
                clearOK = true
                UINotificationFeedbackGenerator().notificationOccurred(.success)
            }
        case .detect:
            if r.blade {
                if stableSince == nil { stableSince = now }
                if heldFor >= detectHold && !detectOK {
                    detectOK = true
                    UINotificationFeedbackGenerator().notificationOccurred(.success)
                }
            } else if !detectOK {
                stableSince = nil
            }
        case .done:
            break
        }
    }
}

// MARK: - Small views

/// Big live indicator of what the sensor sees right now.
private struct SensorLight: View {
    let reading: IRCalReading
    let connected: Bool

    var body: some View {
        HStack(spacing: 14) {
            Circle()
                .fill(color)
                .frame(width: 44, height: 44)
                .overlay(Image(systemName: icon).font(.title3.bold()).foregroundStyle(.white))
                .shadow(color: color.opacity(0.5), radius: reading.blade ? 10 : 0)
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.title3.bold())
                Text(subtitle).font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
        }
        .padding(12)
        .background(color.opacity(0.12), in: RoundedRectangle(cornerRadius: 12))
        .animation(.easeOut(duration: 0.12), value: reading.blade)
    }

    private var waiting: Bool { !connected || !reading.received }
    private var color: Color { waiting ? .gray : (reading.blade ? .orange : .green) }
    private var icon: String { waiting ? "ellipsis" : (reading.blade ? "eye.fill" : "checkmark") }
    private var title: String {
        if !connected { return "Not connected" }
        if !reading.received { return "Waiting for the sensor…" }
        return reading.blade ? "Blade detected" : "Clear"
    }
    private var subtitle: String {
        if waiting { return "Readings arrive 20 times a second once connected" }
        return reading.blade ? "The sensor's LED should be on" : "The sensor's LED should be off"
    }
}

/// Simple top view: IR sensor at the bottom, two-blade propeller in the middle.
private struct BladeSketch: View {
    let bladeOnSensor: Bool

    var body: some View {
        ZStack {
            Capsule()
                .fill(LinearGradient(colors: [Color(red: 0.73, green: 0.05, blue: 0.18), .black],
                                     startPoint: .top, endPoint: .bottom))
                .frame(width: 16, height: 130)
                .rotationEffect(.degrees(bladeOnSensor ? 0 : 90))
            Circle().fill(Color(.systemGray)).frame(width: 18, height: 18)
            VStack {
                Spacer()
                HStack(spacing: 6) {
                    RoundedRectangle(cornerRadius: 3).fill(Color(.systemGray2)).frame(width: 34, height: 14)
                        .overlay(Text("IR").font(.system(size: 9, weight: .bold)).foregroundStyle(.white))
                }
            }
        }
        .frame(height: 160)
        .frame(maxWidth: .infinity)
        .accessibilityLabel(bladeOnSensor ? "One blade pointing at the sensor" : "Blades pointing away from the sensor")
    }
}

private struct Hint: View {
    let icon: String
    let color: Color
    let text: String

    var body: some View {
        Label {
            Text(text).font(.callout)
        } icon: {
            Image(systemName: icon).foregroundStyle(color)
        }
        .padding(12)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(color.opacity(0.1), in: RoundedRectangle(cornerRadius: 12))
    }
}

private extension View {
    func card() -> some View {
        padding()
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(Color(.secondarySystemGroupedBackground), in: RoundedRectangle(cornerRadius: 16))
    }
}

#Preview("IR calibration") {
    IRCalibrationView()
        .environmentObject(PARMCOLink(demo: true))
}
