//
//  PARMCOApp.swift
//  PARMCO iPhone controller (ECSE 4235, Group 2)
//
//  AI-generated (Claude).
//

import SwiftUI

@main
struct PARMCOApp: App {
    @StateObject private var link = PARMCOLink()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(link)
        }
    }
}
