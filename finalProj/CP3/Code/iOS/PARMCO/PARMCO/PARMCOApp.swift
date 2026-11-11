//
//  PARMCOApp.swift
//  PARMCO iPhone controller (ECSE 4235, Group 2)
//
//  AI-generated (Claude).
//

import SwiftUI

@main
struct PARMCOApp: App {
    // The Simulator has no Bluetooth, so it runs in demo mode with sample
    // readings; a real iPhone always uses Bluetooth.
    #if targetEnvironment(simulator)
    @StateObject private var link = PARMCOLink(demo: true)
    #else
    @StateObject private var link = PARMCOLink()
    #endif

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(link)
        }
    }
}
