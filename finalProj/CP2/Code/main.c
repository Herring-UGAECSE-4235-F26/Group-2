#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <poll.h>
#include <fcntl.h>
#include "E4235.h"

extern void enable_routine(int pin, int state);
extern void step_routine(int direction, int mode, int* pins);
extern void step_timing_loop(unsigned int ticks);
extern unsigned int get_timer_frequency(void);
extern void increment_edge_count(void);
extern int get_and_reset_count(void);
extern unsigned int get_current_timer(void);

#define PIN_ENABLE 24
#define PIN_A 17
#define PIN_B 27
#define PIN_C 22
#define PIN_D 23
#define PIN_IR_SENSOR 13 // Changed to GPIO 13

#define TIMER_FREQ 54000000
#define PROPELLER_BLADES 2 // Adjust based on your physical propeller

volatile int run_program = 1;

void handle_sigint(int sig) {
    run_program = 0;
}

void* ir_interrupt_thread(void* arg) {
    // Setup sysfs path for GPIO 13
    int fd = open("/sys/class/gpio/gpio13/value", O_RDONLY);
    if (fd < 0) {
        printf("\n[!] IR Thread Error: Could not open GPIO 13 sysfs value.\n");
        printf("    Ensure you run these terminal commands before executing:\n");
        printf("    echo 13 > /sys/class/gpio/export\n");
        printf("    echo falling > /sys/class/gpio/gpio13/edge\n");
        return NULL;
    }

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLPRI; // Wait for urgent data (hardware interrupt edge)
    char buf[8];

    while(run_program) {
        lseek(fd, 0, SEEK_SET);
        read(fd, buf, sizeof(buf)); // Clear previous interrupt
        
        // Poll with 500ms timeout so the thread can exit gracefully when run_program goes to 0
        int ret = poll(&pfd, 1, 500); 
        if (ret > 0 && (pfd.revents & POLLPRI)) {
            increment_edge_count(); // Trigger Assembly Counter
        }
    }
    close(fd);
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

    // Start IR Sensor Interrupt Thread
    pthread_t ir_thread;
    if(pthread_create(&ir_thread, NULL, ir_interrupt_thread, NULL) != 0) {
        printf("Failed to start IR sensor thread.\n");
    }

    int mode = 0;           
    int direction = 1;      
    int running = 0;        
    int enabled = 0;        
    float steps_per_sec = 100.0; 
    
    // Initial fetch to flush any startup noise and establish timing baseline
    unsigned int last_rpm_time = get_current_timer();
    get_and_reset_count(); 

    printf("PARMCO Motor Controller CP2 Initialized.\n");
    printf("Step Mode: %s\n", mode == 0 ? "Full Step (200/rev)" : "Half Step (400/rev)"); 
    printf("Controls: [E]nable, [C]irection, [Space] One rev, [S]tart/Stop, [U] Faster, [D] Slower, [T]est Step, [R]ead RPM, [Q]uit\n");

    E4235_KYBdeblock(1); 
    char key = 0;
    
    while (run_program) {
        while (read(STDIN_FILENO, &key, 1) > 0) {
            if (key == '\n' || key == '\r') continue;

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
                        printf("\nAssembly step executed.\n");
                    } else {
                        printf("\nCannot step: Motor is currently DISABLED and will not move.\n");
                    }
                    break;
                case 'r':
                case 'R': {
                    // Checkpoint 2: Highly accurate RPM Calculation using the ARM 64-bit Timer
                    unsigned int current_time = get_current_timer();
                    unsigned int elapsed_ticks = current_time - last_rpm_time;
                    float elapsed_seconds = (float)elapsed_ticks / TIMER_FREQ;
                    
                    int edges = get_and_reset_count();
                    float rpm = (edges / (float)PROPELLER_BLADES) * (60.0 / elapsed_seconds);
                    
                    printf("\n--- RPM Check ---\n");
                    printf("Edges detected: %d\n", edges);
                    printf("Elapsed time: %.3f sec\n", elapsed_seconds);
                    printf("Actual Speed: %.2f RPM\n", rpm);
                    printf("-----------------\n");
                    
                    last_rpm_time = current_time;
                    break;
                }
                case 'q':
                case 'Q': 
                    run_program = 0;
                    break;
            }
            key = 0;
        }

        if (running && enabled) {
            unsigned int tick_delay = (unsigned int)(TIMER_FREQ / steps_per_sec);
            step_routine(direction, mode, motor_pins);
            step_timing_loop(tick_delay);
        } else {
            usleep(10000); 
        }
    }

    enable_routine(PIN_ENABLE, 0); 
    E4235_Select(PIN_ENABLE, 0); 
    for (int i = 0; i < 4; i++) {
        E4235_Write(motor_pins[i], 0); 
        E4235_Select(motor_pins[i], 0); 
    }
    
    E4235_KYBdeblock(0); 
    pthread_join(ir_thread, NULL); // Safely close the IR thread
    printf("\nExiting PARMCO safely.\n");
    return 0;
}
