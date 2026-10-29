#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include "E4235.h"

extern void enable_routine(int pin, int state);
extern void step_routine(int direction, int mode, int* pins);
extern void step_timing_loop(unsigned int ticks);
extern unsigned int get_timer_frequency(void);
extern void increment_edge_count(void);
extern int get_and_reset_count(void);
extern unsigned int get_current_timer(void);

// Expose the internal memory pointer from the E4235 library
extern volatile unsigned int *gpio_mem_ptr;

#define PIN_ENABLE 24
#define PIN_A 17
#define PIN_B 27
#define PIN_C 22
#define PIN_D 23
#define PIN_IR_SENSOR 13

#define TIMER_FREQ 54000000
#define PROPELLER_BLADES 2 

volatile int run_program = 1;

void handle_sigint(int sig) {
    run_program = 0;
}

// Background thread to poll the GPIO memory register at 1000 Hz
void* ir_polling_thread(void* arg) {
    int last_state = 1; // Assume HIGH (unbroken beam) initially
    
    while(run_program) {
        // GPLEV0 (GPIO Level 0) register is at offset 0x34 bytes (index 13)
        // Bit 13 corresponds to the physical state of GPIO 13
        int current_state = (gpio_mem_ptr[13] & (1 << PIN_IR_SENSOR)) ? 1 : 0;
        
        // Detect falling edge: beam was unbroken (1), now broken (0)
        if (last_state == 1 && current_state == 0) {
            increment_edge_count(); // Trigger Assembly Counter
        }
        
        last_state = current_state;
        
        // Sleep 1 millisecond (1000 Hz sampling rate) to prevent 100% CPU usage
        usleep(1000); 
    }
    return NULL;
}

int main() {
    signal(SIGINT, handle_sigint);

    if (E4235_Init() != 0) { 
        printf("Failed to initialize E4235 library.\n");
        return -1;
    }

    unsigned int actual_freq = get_timer_frequency();
    printf("ARM Generic Timer Frequency: %u Hz\n", actual_freq);
    
    E4235_Select(PIN_ENABLE, 1); 
    int motor_pins[4] = {PIN_A, PIN_B, PIN_C, PIN_D};
    for (int i = 0; i < 4; i++) {
        E4235_Select(motor_pins[i], 1); 
    }

    // Configure GPIO 13 as an INPUT (0) so it doesn't fight the sensor
    E4235_Select(PIN_IR_SENSOR, 0);

    // Start IR Sensor Polling Thread
    pthread_t ir_thread;
    if(pthread_create(&ir_thread, NULL, ir_polling_thread, NULL) != 0) {
        printf("Failed to start IR sensor thread.\n");
    }

    int mode = 0;           
    int direction = 1;      
    int running = 0;        
    int enabled = 0;        
    float steps_per_sec = 100.0; 
    
    unsigned int last_rpm_time = get_current_timer();
    get_and_reset_count(); 

    // Moving average variables to smooth out RPM jitter at low speeds
    #define RPM_WINDOW 4
    int edge_history[RPM_WINDOW] = {0};
    int history_idx = 0;

    printf("PARMCO Motor Controller CP2 Initialized.\n");
    printf("Step Mode: %s\n", mode == 0 ? "Full Step (200/rev)" : "Half Step (400/rev)"); 
    printf("Controls: [E]nable, [C]irection, [Space] One rev, [S]tart/Stop, [U] Faster, [D] Slower, [T]est Step, [Q]uit\n\n");

    E4235_KYBdeblock(1); 
    char key = 0;
    
    while (run_program) {
        while (read(STDIN_FILENO, &key, 1) > 0) {
            if (key == '\n' || key == '\r') continue;

            // Clear the continuous status line before printing new terminal output
            printf("\r                                                                                \r");

            switch (key) {
                case 'e':
                case 'E': 
                    enabled = !enabled;
                    enable_routine(PIN_ENABLE, enabled);
                    if (enabled) {
                        step_routine(direction, mode, motor_pins);
                    } else {
                        for (int i = 0; i < 4; i++) E4235_Write(motor_pins[i], 0); 
                    }
                    printf("Motor %s.\n", enabled ? "ENABLED" : "DISABLED");
                    break;
                case 'c':
                case 'C': 
                    direction = !direction;
                    printf("Direction: %s\n", direction ? "Clockwise" : "Counterclockwise");
                    break;
                case 's':
                case 'S': 
                    running = !running;
                    printf("Motor %s in %s mode.\n", running ? "RUNNING" : "STOPPED", mode == 0 ? "Full Step" : "Half Step");
                    break;
                case 'u':
                case 'U': 
                    steps_per_sec *= 1.02; 
                    printf("Speed increased. Current rate: %.2f steps/sec (%s)\n", steps_per_sec, mode == 0 ? "Full Step" : "Half Step");
                    break;
                case 'd':
                case 'D': 
                    steps_per_sec *= 0.98; 
                    if (steps_per_sec < 10.0) steps_per_sec = 10.0; 
                    printf("Speed decreased. Current rate: %.2f steps/sec (%s)\n", steps_per_sec, mode == 0 ? "Full Step" : "Half Step");
                    break;
                case ' ': 
                    if (enabled) {
                        printf("Executing slow constant-speed revolution...\n");
                        int steps_needed = (mode == 0) ? 200 : 400;
                        unsigned int tick_delay = (unsigned int)(TIMER_FREQ / 22.0); 
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
                case 't':
                case 'T': 
                    running = 0; 
                    if (enabled) {
                        step_routine(direction, mode, motor_pins); 
                        printf("Assembly step executed.\n");
                    } else {
                        printf("Cannot step: Motor is currently DISABLED and will not move.\n");
                    }
                    break;
                case 'r':
                case 'R': {
                    printf("--- Manual RPM Check Triggered ---\n");
                    break;
                }
                case 'q':
                case 'Q': 
                    run_program = 0;
                    break;
            }
            key = 0;
        }

        // CP2 Requirement: Continuous Status Display with Moving Average
        unsigned int current_time = get_current_timer();
        if ((current_time - last_rpm_time) >= (TIMER_FREQ / 2)) { // Trigger every 0.5 seconds
            float elapsed_seconds = (float)(current_time - last_rpm_time) / TIMER_FREQ;
            int edges = get_and_reset_count();
            
            // Insert latest edge count into the circular buffer
            edge_history[history_idx] = edges;
            history_idx = (history_idx + 1) % RPM_WINDOW;
            
            // Sum the historical window
            int total_edges = 0;
            for(int i = 0; i < RPM_WINDOW; i++) {
                total_edges += edge_history[i];
            }
            
            // Calculate RPM based on the total historical window time (4 * 0.5s = 2.0 seconds)
            float total_window_time = elapsed_seconds * RPM_WINDOW;
            float rpm = (total_edges / (float)PROPELLER_BLADES) * (60.0 / total_window_time);
            
            // Zero out residual RPM instantly if the motor is explicitly stopped
            if (!running || !enabled) {
                rpm = 0.0;
                for(int i = 0; i < RPM_WINDOW; i++) edge_history[i] = 0; // flush buffer
            }

            // Print status bar with a carriage return to overwrite the line
            printf("\r[STATUS] Motor: %-7s | Dir: %-3s | Target: %5.1f steps/s | RPM: %6.2f    ", 
                (running && enabled) ? "RUNNING" : "STOPPED",
                direction ? "CW" : "CCW",
                steps_per_sec,
                rpm);
            fflush(stdout); // Force print to console immediately
            
            last_rpm_time = current_time;
        }

        if (running && enabled) {
            unsigned int tick_delay = (unsigned int)(TIMER_FREQ / steps_per_sec);
            step_routine(direction, mode, motor_pins);
            step_timing_loop(tick_delay);
        } else {
            usleep(10000); 
        }
    }

    // Clear status bar on exit
    printf("\r                                                                                \r");
    enable_routine(PIN_ENABLE, 0); 
    E4235_Select(PIN_ENABLE, 0); 
    for (int i = 0; i < 4; i++) {
        E4235_Write(motor_pins[i], 0); 
        E4235_Select(motor_pins[i], 0); 
    }
    
    E4235_KYBdeblock(0); 
    pthread_join(ir_thread, NULL); 
    printf("Exiting PARMCO safely.\n");
    return 0;
}