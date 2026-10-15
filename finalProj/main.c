#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "E4235.h" // Class library header[cite: 46]

// External assembly functions from motor_driver.s
extern void enable_routine(int pin, int state);
extern void step_routine(int direction, int mode, int* pins);
extern void step_timing_loop(unsigned int ticks);

// Define GPIO pins (Adjust these to match your actual wiring)
#define PIN_ENABLE 24
#define PIN_A 17
#define PIN_B 27
#define PIN_C 22
#define PIN_D 23

// RP4 ARM Generic Timer frequency (typically 54 MHz)
#define TIMER_FREQ 54000000

int main() {
    // 1. Initialize Hardware and GPIO
    if (E4235_Init() != 0) { //[cite: 46, 56]
        printf("Failed to initialize E4235 library.\n");
        return -1;
    }

    // Set motor and enable pins to OUTPUT (state = 1)
    E4235_Select(PIN_ENABLE, 1); //[cite: 46, 56, 57]
    int motor_pins[4] = {PIN_A, PIN_B, PIN_C, PIN_D};
    for (int i = 0; i < 4; i++) {
        E4235_Select(motor_pins[i], 1); //[cite: 46, 56, 57]
    }

    // 2. Initial State Variables
    int mode = 0;           // 0 = Full step, 1 = Half step
    int direction = 1;      // 1 = Clockwise, 0 = Counterclockwise
    int running = 0;        // 0 = Stopped, 1 = Continuous Run
    int enabled = 0;        // 0 = Disabled, 1 = Enabled
    float steps_per_sec = 100.0; // Initial step rate
    
    // Checkpoint 1 requirement: Print step mode at startup[cite: 7]
    printf("PARMCO Motor Controller Initialized.\n");
    printf("Step Mode: %s\n", mode == 0 ? "Full Step (200/rev)" : "Half Step (400/rev)");
    printf("Controls: [E]nable, [C]irection, [Space] One rev, [S]tart/Stop, [Up] Faster, [Down] Slower, [Q]uit\n");

    // 3. Unblock standard input for non-blocking keyboard polling
    E4235_KYBdeblock(1); //[cite: 46, 52, 53]

    char buf[3];
    int run_program = 1;

    while (run_program) {
        // Read up to 3 bytes to catch arrow key escape sequences
        int bytes_read = read(STDIN_FILENO, buf, 3);
        
        if (bytes_read > 0) {
            if (bytes_read == 1) {
                switch (buf[0]) {
                    case 'e':
                    case 'E': // Enable/Disable H-Bridge
                        enabled = !enabled;
                        enable_routine(PIN_ENABLE, enabled);
                        printf("Motor %s.\n", enabled ? "ENABLED" : "DISABLED");
                        break;
                    
                    case 'c':
                    case 'C': // Toggle Direction
                        direction = !direction;
                        printf("Direction: %s\n", direction ? "Clockwise" : "Counterclockwise");
                        break;

                    case ' ': // Spacebar: One Revolution
                        if (enabled) {
                            printf("Executing one revolution...\n");
                            int steps_needed = (mode == 0) ? 200 : 400; //[cite: 7]
                            unsigned int tick_delay = (unsigned int)(TIMER_FREQ / steps_per_sec);
                            for (int i = 0; i < steps_needed; i++) {
                                step_routine(direction, mode, motor_pins);
                                step_timing_loop(tick_delay);
                            }
                            running = 0; // Ensure we stop after one revolution
                            printf("Revolution complete.\n");
                        } else {
                            printf("Cannot step: Motor is disabled.\n");
                        }
                        break;

                    case 's':
                    case 'S': // Continuous Run / Stop
                        running = !running;
                        printf("Motor %s.\n", running ? "RUNNING" : "STOPPED");
                        break;

                    case 'q':
                    case 'Q': // Quit
                        run_program = 0;
                        break;
                }
            } else if (bytes_read == 3 && buf[0] == '\033' && buf[1] == '[') {
                // Arrow keys generate a 3-byte sequence: ESC (0x1B), '[', and 'A', 'B', 'C', or 'D'
                if (buf[2] == 'A') { // Up Arrow: Faster (increase by ~5%)[cite: 7]
                    steps_per_sec *= 1.05;
                    printf("Speed increased. Current rate: %.2f steps/sec\n", steps_per_sec);
                } else if (buf[2] == 'B') { // Down Arrow: Slower (decrease by ~5%)[cite: 7]
                    steps_per_sec *= 0.95;
                    if (steps_per_sec < 10.0) steps_per_sec = 10.0; // Prevent stalling at zero
                    printf("Speed decreased. Current rate: %.2f steps/sec\n", steps_per_sec);
                }
            }
        }

        // 4. Continuous Running Logic
        if (running && enabled) {
            unsigned int tick_delay = (unsigned int)(TIMER_FREQ / steps_per_sec);
            step_routine(direction, mode, motor_pins);
            step_timing_loop(tick_delay);
        } else {
            // Sleep briefly to prevent 100% CPU usage when idle
            usleep(10000); 
        }
    }

    // 5. Cleanup before exit
    running = 0;
    enable_routine(PIN_ENABLE, 0);
    E4235_KYBdeblock(0); // Restore blocking IO[cite: 46, 52, 53]
    printf("Exiting PARMCO.\n");
    return 0;
}