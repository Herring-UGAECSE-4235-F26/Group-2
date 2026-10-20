#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "E4235.h" // Class library header[cite: 46]

// External assembly functions from motor_driver.s
extern void enable_routine(int pin, int state);
extern void step_routine(int direction, int mode, int* pins);
extern void step_timing_loop(unsigned int ticks);
extern unsigned int get_timer_frequency(void);

// Define GPIO pins (Updated to match actual wiring)
#define PIN_ENABLE 24
#define PIN_A 17
#define PIN_B 27
#define PIN_C 22
#define PIN_D 23

// RP4 ARM Generic Timer frequency (typically 54 MHz)
#define TIMER_FREQ 54000000

// Global flag for the main loop, allowing the signal handler to gracefully exit
volatile int run_program = 1;

// Signal handler to catch Ctrl+C and ensure cleanup executes
void handle_sigint(int sig) {
    run_program = 0;
}

void test_motor_gpio(void) {
    int pins[4] = {17, 27, 22, 23};
    int driver_pins[4] = {2, 7, 10, 15};
    char input[10];

    printf("\nDo you want to run the motor GPIO test? (y/n): ");
    if (fgets(input, sizeof(input), stdin) != NULL) {
        if (input[0] != 'y' && input[0] != 'Y') {
            printf("Skipping motor GPIO test.\n\n");
            return;
        }
    }

    printf("\nStarting motor GPIO test...\n");

    // Keep the H-bridge disabled during testing
    E4235_Write(PIN_ENABLE, 0); //[cite: 46, 57]

    // Initialize all motor GPIOs LOW
    for (int i = 0; i < 4; i++) {
        E4235_Write(pins[i], 0); //[cite: 46, 57]
    }

    for (int i = 0; i < 4; i++) {

        // Set all motor GPIOs LOW
        for (int j = 0; j < 4; j++) {
            E4235_Write(pins[j], 0); //[cite: 46, 57]
        }

        // Set the GPIO being tested HIGH
        E4235_Write(pins[i], 1); //[cite: 46, 57]

        printf("\nGPIO%d is HIGH.\n", pins[i]);
        printf("Measure SN754410NE pin %d.\n", driver_pins[i]);
        printf("Expected voltage: approximately 3.3 V\n");
        printf("Press Enter to continue, or 'q' then Enter to quit test...\n");

        if (fgets(input, sizeof(input), stdin) != NULL) {
            if (input[0] == 'q' || input[0] == 'Q') {
                printf("Aborting motor GPIO test.\n");
                break;
            }
        }
    }

    // Return all GPIOs LOW after testing
    for (int i = 0; i < 4; i++) {
        E4235_Write(pins[i], 0); //[cite: 46, 57]
    }

    printf("\nMotor GPIO test sequence finished.\n\n");
}

int main() {
    // Register the signal handler for SIGINT (Ctrl+C)
    signal(SIGINT, handle_sigint);

    // 1. Initialize Hardware and GPIO
    if (E4235_Init() != 0) { //[cite: 46, 56]
        printf("Failed to initialize E4235 library.\n");
        return -1;
    }

    unsigned int actual_freq = get_timer_frequency();

    printf("ARM Generic Timer Frequency: %u Hz\n", actual_freq);
    printf("Currently Configured Frequency: %u Hz\n", TIMER_FREQ);

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
    
    // Checkpoint 1 requirement: Print step mode at startup
    printf("PARMCO Motor Controller Initialized.\n");
    printf("Step Mode: %s\n", mode == 0 ? "Full Step (200/rev)" : "Half Step (400/rev)"); //[cite: 7]
    printf("Controls: [E]nable, [C]irection, [Space] One rev, [S]tart/Stop, [U] Faster, [D] Slower, [T]est Step, [Q]uit\n");

    // Run the user-defined GPIO hardware test before unblocking the keyboard
    //test_motor_gpio();

    // 3. Unblock standard input for non-blocking keyboard polling
    E4235_KYBdeblock(1); //[cite: 46, 52, 53]

    char key = 0;

    while (run_program) {
        // Read strictly 1 byte at a time in a WHILE loop to instantly drain the buffer
        while (read(STDIN_FILENO, &key, 1) > 0) {
            
            // Ignore trailing newlines or carriage returns
            if (key == '\n' || key == '\r') {
                continue;
            }

            switch (key) {
                case 'e':
                case 'E': // Enable/Disable H-Bridge
                    enabled = !enabled;
                    enable_routine(PIN_ENABLE, enabled);
                    
                    if (enabled) {
                        // Software Fix: Apply the current coil pattern immediately 
                        // so the motor snaps into holding torque right when enabled.
                        step_routine(direction, mode, motor_pins);
                    } else {
                        // Software Failsafe: De-energize all coils. This forces the 
                        // motor to free up even if the hardware enable pins are floating[cite: 8].
                        for (int i = 0; i < 4; i++) {
                            E4235_Write(motor_pins[i], 0); //[cite: 46, 57]
                        }
                    }
                    
                    printf("Motor %s.\n", enabled ? "ENABLED" : "DISABLED");
                    break;
                case 'c':
                case 'C': // Toggle Direction
                    direction = !direction;
                    printf("Direction: %s\n", direction ? "Clockwise" : "Counterclockwise");
                    break;

                case ' ': // Spacebar: One Revolution (Crawl Test)
                    if (enabled) {
                        printf("Executing slow constant-speed revolution to test mechanics...\n");
                        int steps_needed = (mode == 0) ? 200 : 400;
                        
                        // Force a very slow 20 steps/sec speed with NO acceleration ramp
                        unsigned int tick_delay = (unsigned int)(TIMER_FREQ / 20.0); 
                        
                        for (int i = 0; i < steps_needed; i++) {
                            step_routine(direction, mode, motor_pins);
                            step_timing_loop(tick_delay);
                        }
                        running = 0; 
                        printf("Revolution complete.\n");
                    } else {
                        printf("Cannot step: Motor is disabled.\n");
                    }
                    break;

                case 's':
                case 'S': // Continuous Run / Stop
                    running = !running;
                    printf("Motor %s in %s mode.\n", running ? "RUNNING" : "STOPPED", mode == 0 ? "Full Step" : "Half Step");
                    break;
                    
                case 'u':
                case 'U': // Faster (increase by ~2%)
                    steps_per_sec *= 1.02; //[cite: 7]
                    printf("Speed increased. Current rate: %.2f steps/sec (%s)\n", steps_per_sec, mode == 0 ? "Full Step" : "Half Step");
                    break;

                case 'd':
                case 'D': // Slower (decrease by ~5%)
                    steps_per_sec *= 0.95; //[cite: 7]
                    if (steps_per_sec < 10.0) steps_per_sec = 10.0; // Prevent stalling at zero
                    printf("Speed decreased. Current rate: %.2f steps/sec (%s)\n", steps_per_sec, mode == 0 ? "Full Step" : "Half Step");
                    break;

		        case 't':
		        case 'T': // Test one assembly motor step
		            running = 0; // Stop continuous run

                    if (enabled) {
                        // Advance the assembly stepping sequence once
                        step_routine(direction, mode, motor_pins); 
                        
                        printf("\nAssembly step executed.\n");
                        printf("Direction: %s\n", direction ? "Clockwise" : "Counterclockwise");
                        printf("Motor stepped. Measure SN754410NE input pins 2, 7, 10, and 15 if needed.\n");
                        printf("Press T to advance to the next step.\n\n");
                    } else {
                        printf("\nCannot step: Motor is currently DISABLED and will not move.\n");
                        printf("Press E to enable the motor first.\n\n");
                    }
                    break;
                case 'q':
                case 'Q': // Quit
                    run_program = 0;
                    break;
            }
            key = 0; 
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
    // Drive the 4N25 optocoupler low and set to input so it floats
    enable_routine(PIN_ENABLE, 0); //[cite: 8]
    E4235_Select(PIN_ENABLE, 0); //[cite: 46, 56, 57]
    
    // De-energize and float the motor coils
    for (int i = 0; i < 4; i++) {
        E4235_Write(motor_pins[i], 0); //[cite: 46, 57]
        E4235_Select(motor_pins[i], 0); //[cite: 46, 56, 57]
    }
    
    E4235_KYBdeblock(0); // Restore blocking IO[cite: 46, 52, 53]
    printf("\nExiting PARMCO safely.\n");
    return 0;
}
