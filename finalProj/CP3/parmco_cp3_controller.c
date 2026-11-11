#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include "E4235.h"

extern void enable_routine(int pin, int state);
extern void step_routine(int direction, int mode, int *pins);
extern void step_timing_loop(unsigned int ticks);
extern unsigned int get_timer_frequency(void);
extern void increment_edge_count(void);
extern int get_and_reset_count(void);
extern unsigned int get_current_timer(void);
extern volatile unsigned int *gpio_mem_ptr;

#define PIN_ENABLE 24
#define PIN_A 17
#define PIN_B 27
#define PIN_C 22
#define PIN_D 23
#define PIN_IR_SENSOR 13
#define PROPELLER_BLADES 2
#define RFCOMM_CHANNEL 1
#define MAX_COMMAND 128

static volatile sig_atomic_t run_program = 1;
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static int running = 0;
static int direction = 1;       /* 1=CW, 0=CCW */
static int mode = 0;            /* 0=full-step, 1=half-step */
static float steps_per_sec = 200.0f * 20.0f / 60.0f;
static unsigned int timer_freq = 54000000U;
static int server_fd = -1;

static void handle_signal(int sig) { (void)sig; run_program = 0; }

static void stop_motor(void) {
    pthread_mutex_lock(&state_lock);
    running = 0;
    pthread_mutex_unlock(&state_lock);
}

static int send_message(int fd, const char *message) {
    char buffer[256];
    int length = snprintf(buffer, sizeof(buffer), "%s\n", message);
    if (length < 0 || length >= (int)sizeof(buffer)) return -1;
    for (int sent = 0; sent < length;) {
        ssize_t n = send(fd, buffer + sent, (size_t)(length - sent), MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        sent += (int)n;
    }
    return 0;
}

static int process_command(int fd, const char *cmd) {
    char response[160];
    int valid = 1;
    printf("Received: %s\n", cmd);
    pthread_mutex_lock(&state_lock);
    if (!strcmp(cmd, "START")) {
        running = 1;
    } else if (!strcmp(cmd, "STOP")) {
        running = 0;
    } else if (!strcmp(cmd, "DIR:CW")) {
        direction = 1;
    } else if (!strcmp(cmd, "DIR:CCW")) {
        direction = 0;
    } else if (!strcmp(cmd, "FASTER")) {
        steps_per_sec *= 1.02f;
        if (steps_per_sec > 200.0f * 100.0f / 60.0f)
            steps_per_sec = 200.0f * 100.0f / 60.0f;
    } else if (!strcmp(cmd, "SLOWER")) {
        steps_per_sec *= 0.98f;
        if (steps_per_sec < 200.0f * 10.0f / 60.0f)
            steps_per_sec = 200.0f * 10.0f / 60.0f;
    } else if (!strncmp(cmd, "RPM:", 4)) {
        char *end = NULL;
        errno = 0;
        float rpm = strtof(cmd + 4, &end);
        if (errno || end == cmd + 4 || *end != '\0' || !(rpm >= 10.0f && rpm <= 100.0f)) {
            snprintf(response, sizeof(response), "ERR:INVALID_RPM");
            valid = 0;
        } else {
            steps_per_sec = rpm * (mode ? 400.0f : 200.0f) / 60.0f;
        }
    } else if (!strcmp(cmd, "CALIBRATE_ZERO") || !strcmp(cmd, "SET_ZERO") ||
               !strncmp(cmd, "ANGLE:", 6) || !strcmp(cmd, "MAINTAIN:ON") ||