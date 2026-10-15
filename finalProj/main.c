#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "E4235.h" // Class library header[cite: 46]

// External assembly functions from motor_driver.s
extern void enable_routine(int pin, int state);
extern void step_routine(int direction, int mode, int* pins);
extern void step_timing_loop(unsigned int ticks);

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

    printf("\nStarting motor GPIO test...\n");

    // Keep the H-bridge disabled during testing
    E4235_Write(PIN_ENABLE, 0);[cite: 46, 57]

    // Initialize all motor GPIOs LOW
    for (int i = 0; i < 4; i++) {
        E4235_Write(pins[i], 0);[cite: 46, 57]
    }

    for (int i = 0; i < 4; i++) {

        // Set all motor GPIOs LOW
        for (int j = 0; j < 4; j++) {
            E4235_Write(pins[j], 0);[cite: 46, 57]
        }

        // Set the GPIO being tested HIGH
        E4235_Write(pins[i], 1);[cite: 46, 57]

        printf("\nGPIO%d is HIGH.\n", pins[i]);
        printf("Measure SN754410NE pin %d.\n", driver_pins[i]);
        printf("Expected voltage: approximately 3.3 V\n");
        printf("Press Enter when finished measuring...\n");

        getchar();
    }

    // Return all GPIOs LOW after testing
    for (int i = 0; i < 4; i++) {
        E4235_Write(pins[i], 0);[cite: 46, 57]
    }

    printf("\nAll GPIO tests completed.\n");
}

int main() {
    // Register the signal handler for SIGINT (Ctrl+C)
    signal(SIGINT, handle_sigint);

    // 1. Initialize Hardware and GPIO
    if (E4235_Init() != 0) {[cite: 46, 56]
        printf("Failed to initialize E4235 library.\n");
        return -1;
    }

    // Set motor and enable pins to OUTPUT (state = 1)
    E4235_Select(PIN_ENABLE, 1);[cite: 46, 56, 57]
    int motor_pins[4] = {PIN_A, PIN_B, PIN_C, PIN_D};
    for (int i = 0; i < 4; i++) {
        E4235_Select(motor_pins[i], 1);[cite: 46, 56, 57]
    }

    // 2. Initial State Variables
    int mode = 0;           // 0 = Full step, 1 =