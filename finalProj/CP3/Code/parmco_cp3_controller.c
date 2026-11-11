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
#include <time.h>
#include "as5600.h"

extern void enable_routine(int pin, int state);
extern void step_routine(int direction, int mode, int *pins);
extern void step_timing_loop(unsigned int ticks);
extern unsigned int get_timer_frequency(void);
extern void increment_edge_count(void);
extern int get_and_reset_count(void);
extern unsigned int get_current_timer(void);
extern volatile unsigned int *gpio_mem_ptr;
static double monotonic_seconds(void);

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
static pthread_mutex_t ir_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t encoder_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned int ir_edges = 0;
static float latest_ir_rpm = 0.0f;
static int encoder_available = 0;
static float encoder_zero = 0.0f;
static float latest_encoder_angle = 0.0f;
static float latest_encoder_rpm = 0.0f;
static float previous_encoder_angle = 0.0f;
static double previous_encoder_time = 0.0;
static int encoder_rpm_initialized = 0;

static void handle_signal(int sig) { (void)sig; run_program = 0; }

/*
 * read_encoder_angle:
 * Reads the AS5600 encoder and calculates its angle
 * relative to the current zero reference.
 *
 * Returns 0 on success and -1 on failure.
 */
static int read_encoder_angle(float *angle)
{
    float raw_degrees;

    if (angle == NULL || !encoder_available)
        return -1;

    pthread_mutex_lock(&encoder_lock);

    int result = as5600_read_degrees(&raw_degrees);

    if (result != 0) {
        pthread_mutex_unlock(&encoder_lock);
        return -1;
    }

    float relative_angle = raw_degrees - encoder_zero;

    if (relative_angle < 0.0f)
        relative_angle += 360.0f;

    if (relative_angle >= 360.0f)
        relative_angle -= 360.0f;

    latest_encoder_angle = relative_angle;
    *angle = relative_angle;

    pthread_mutex_unlock(&encoder_lock);

    return 0;
}

/*
 * update_encoder_rpm:
 * Calculates rotational speed from consecutive AS5600 readings.
 * Returns 0 on success and -1 if the encoder is unavailable.
 */
static int update_encoder_rpm(float *rpm)
{
    float current_angle;

    if (rpm == NULL || read_encoder_angle(&current_angle) != 0)
        return -1;

    double now = monotonic_seconds();

    if (!encoder_rpm_initialized) {
        previous_encoder_angle = current_angle;
        previous_encoder_time = now;
        encoder_rpm_initialized = 1;
        *rpm = 0.0f;
        return 0;
    }

    double elapsed = now - previous_encoder_time;
    float delta = current_angle - previous_encoder_angle;

    /* Correct for crossing the 0/360-degree boundary. */
    if (delta > 180.0f)
        delta -= 360.0f;
    else if (delta < -180.0f)
        delta += 360.0f;

    if (elapsed <= 0.0)
        return -1;

    float calculated_rpm =
    (delta / 360.0f) * (60.0f / (float)elapsed);

    pthread_mutex_lock(&encoder_lock);
    latest_encoder_rpm = calculated_rpm;
    pthread_mutex_unlock(&encoder_lock);

    previous_encoder_angle = current_angle;
    previous_encoder_time = now;

    *rpm = calculated_rpm;
    return 0;
}

/*
 * encoder_thread:
 * Samples the AS5600 every 10 ms to update encoder RPM.
 */
static void *encoder_thread(void *arg)
{
    (void)arg;

    while (run_program) {
        if (encoder_available) {
            float rpm;

            if (update_encoder_rpm(&rpm) != 0) {
                pthread_mutex_lock(&state_lock);
                encoder_rpm_initialized = 0;
                latest_encoder_rpm = 0.0f;
                pthread_mutex_unlock(&state_lock);
            }
        }

        usleep(10000);
    }

    return NULL;
}

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

// Returns elapsed time using a monotonic clock
static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (double)ts.tv_sec +
           (double)ts.tv_nsec / 1000000000.0;
}

// Calculates IR RPM from the number of detected edges
static float sample_ir_rpm(double elapsed) {
    pthread_mutex_lock(&ir_lock);

    unsigned int count = ir_edges;
    ir_edges = 0;

    pthread_mutex_unlock(&ir_lock);

    float rpm = 0.0f;

    if (elapsed > 0.0) {
        rpm = (float)(
            count * 60.0 /
            (PROPELLER_BLADES * elapsed)
        );
    }

    pthread_mutex_lock(&state_lock);
    latest_ir_rpm = rpm;
    pthread_mutex_unlock(&state_lock);

    return rpm;
}

static int process_command(
    int fd,
    const char *cmd,
    FILE **log_file,
    double *log_start
) {
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
    } else if (!strcmp(cmd, "LOG:START")) {

        if (*log_file != NULL) {
            snprintf(response, sizeof(response),
                    "ERR:LOG_ALREADY_RUNNING");
        } else {
            time_t now = time(NULL);
            struct tm tm_now;
            char filename[80];

            localtime_r(&now, &tm_now);

            strftime(filename, sizeof(filename),
                    "parmco_log_%Y%m%d_%H%M%S.csv",
                    &tm_now);

            *log_file = fopen(filename, "w");

            if (*log_file == NULL) {
                snprintf(response, sizeof(response),
                        "ERR:LOG_OPEN_FAILED");
            } else {
                fprintf(*log_file,
                        "elapsed_s,commanded_rpm,ir_rpm,running,direction\n");

                fflush(*log_file);

                *log_start = monotonic_seconds();

                snprintf(response, sizeof(response),
                        "LOG:STARTED:%s", filename);
            }
        }

        valid = 0;
    } else if (!strcmp(cmd, "LOG:STOP")) {

        if (*log_file == NULL) {
            snprintf(response, sizeof(response),
                    "ERR:LOG_NOT_RUNNING");
        } else {
            fclose(*log_file);
            *log_file = NULL;

            snprintf(response, sizeof(response),
                    "ACK:LOG:STOP");
        }

        valid = 0;
    } else if (!strcmp(cmd, "STATUS")) {

        float rpm = steps_per_sec * 60.0f /
                    (mode ? 400.0f : 200.0f);

        snprintf(response, sizeof(response),
                "STATUS:%s:%s:%.2f:%.2f:%s",
                running ? "RUNNING" : "STOPPED",
                direction ? "CW" : "CCW",
                rpm,
                latest_ir_rpm,
                *log_file ? "LOGGING" : "NOT_LOGGING");

        valid = 0;
    } else if (!strcmp(cmd, "CALIBRATE_ZERO") ||
         !strcmp(cmd, "SET_ZERO")) {

        float current_angle;

        int read_result = -1;

        if (encoder_available) {
           pthread_mutex_lock(&encoder_lock);
           read_result = as5600_read_degrees(&current_angle);
           pthread_mutex_unlock(&encoder_lock);
        }

        if (read_result != 0) {

            strcpy(response,"ERR:ENCODER_UNAVAILABLE");
            valid = 0;

        } else {
            pthread_mutex_lock(&encoder_lock);

	    encoder_zero = current_angle;
	    latest_encoder_angle = 0.0f;
    	    encoder_rpm_initialized = 0;

	    pthread_mutex_unlock(&encoder_lock);

	    if (!strcmp(cmd, "CALIBRATE_ZERO"))
        	strcpy(response, "ACK:CALIBRATE_ZERO");
	    else
        	strcpy(response, "ACK:SET_ZERO");

    	    valid = 0;
	}
    } else if (!strncmp(cmd, "ANGLE:", 6)) {
         strcpy(response,"ERR:ANGLE_NOT_IMPLEMENTED");
         valid = 0;
    }
      else if (!strcmp(cmd, "MAINTAIN:ON") ||
            !strcmp(cmd, "MAINTAIN:OFF")) {

        snprintf(response, sizeof(response),
                "ERR:MAINTAIN_UNAVAILABLE");

        valid = 0;
    }
    else {
            snprintf(response, sizeof(response), "ERR:UNKNOWN_COMMAND");
            valid = 0;
        }
        if (valid) snprintf(response, sizeof(response), "ACK:%s", cmd);
        pthread_mutex_unlock(&state_lock);
        printf("Sending: %s\n", response);
        fflush(stdout);
        return send_message(fd, response);
    }


static void handle_client(int fd) {
    char command[MAX_COMMAND];
    char buffer[256];

    size_t position = 0;
    int overflow = 0;

    FILE *log_file = NULL;
    double log_start = 0.0;
    double last_sample = monotonic_seconds();

    while (run_program) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        struct timeval timeout = {
            .tv_sec = 0,
            .tv_usec = 200000
        };

        int ready = select(
            fd + 1, &rfds, NULL, NULL, &timeout
        );

        double now = monotonic_seconds();

        // Send measured RPM approximately once per second
        if (now - last_sample >= 1.0) {
            float measured = sample_ir_rpm(
                now - last_sample
            );

            last_sample = now;

            pthread_mutex_lock(&state_lock);

            float commanded =
                steps_per_sec * 60.0f /
                (mode ? 400.0f : 200.0f);

            int active = running;
            int dir = direction;

            pthread_mutex_unlock(&state_lock);

            char telemetry[128];

            snprintf(telemetry, sizeof(telemetry),
                     "RPM_ACTUAL:%.2f", measured);

            if (send_message(fd, telemetry) < 0) {
                break;
            }

            /* Send the current encoder angle to the Android app. */
		float encoder_angle;

		if (read_encoder_angle(&encoder_angle) == 0) {
    			char angle_message[64];

    		snprintf(angle_message, sizeof(angle_message),
             		"ANGLE_ACTUAL:%.2f", encoder_angle);

    		send_message(fd, angle_message);
		}
	    /* Send the latest encoder RPM to the Android app. */
           if (encoder_available) {
    		float encoder_rpm;

    		pthread_mutex_lock(&encoder_lock);
    		encoder_rpm = latest_encoder_rpm;
    		pthread_mutex_unlock(&encoder_lock);

    		char rpm_message[64];

    		snprintf(rpm_message, sizeof(rpm_message),
             		"ENCODER_RPM:%.2f", encoder_rpm);

    		send_message(fd, rpm_message);
		}
            // Save one row per second when logging
            if (log_file != NULL) {
                fprintf(log_file,
                        "%.2f,%.2f,%.2f,%d,%s\n",
                        now - log_start,
                        commanded,
                        measured,
                        active,
                        dir ? "CW" : "CCW");

                fflush(log_file);
            }
        }

        if (ready == 0 ||
            (ready < 0 && errno == EINTR)) {
            continue;
        }

        if (ready < 0) {
            break;
        }

        ssize_t received = recv(
            fd, buffer, sizeof(buffer), 0
        );

        if (received <= 0) {
            break;
        }

        for (ssize_t i = 0; i < received; i++) {
            char c = buffer[i];

            if (c == '\n') {
                if (overflow) {
                    if (send_message(
                        fd, "ERR:COMMAND_TOO_LONG"
                    ) < 0) {
                        goto client_cleanup;
                    }
                } else if (position > 0) {
                    command[position] = '\0';

                    if (process_command(
                        fd,
                        command,
                        &log_file,
                        &log_start
                    ) < 0) {
                        goto client_cleanup;
                    }
                }

                position = 0;
                overflow = 0;

            } else if (c != '\r' && !overflow) {
                if (position < sizeof(command) - 1) {
                    command[position++] = c;
                } else {
                    overflow = 1;
                }
            }
        }
    }

client_cleanup:
    if (log_file != NULL) {
        fclose(log_file);
    }
}

static void *bluetooth_thread(void *arg) {
    (void)arg;
    int fd = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (fd < 0) { perror("Bluetooth socket"); return NULL; }
    server_fd = fd;
    struct sockaddr_rc local = {0};
    local.rc_family = AF_BLUETOOTH;
    bacpy(&local.rc_bdaddr, BDADDR_ANY);
    local.rc_channel = RFCOMM_CHANNEL;
    if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0 || listen(fd, 1) < 0) {
        perror("Bluetooth bind/listen");
        close(fd);
        server_fd = -1;
        return NULL;
    }
    printf("Bluetooth listening on RFCOMM channel %d\n", RFCOMM_CHANNEL);
    while (run_program) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval timeout = { .tv_sec = 0, .tv_usec = 200000 };
        int ready = select(fd + 1, &rfds, NULL, NULL, &timeout);
        if (ready == 0 || (ready < 0 && errno == EINTR)) continue;
        if (ready < 0) break;
        struct sockaddr_rc remote = {0};
        socklen_t len = sizeof(remote);
        int client = accept(fd, (struct sockaddr *)&remote, &len);
        if (client < 0) continue;
        char address[18];
        ba2str(&remote.rc_bdaddr, address);
        printf("Android connected: %s\n", address);
        if (send_message(client, "PARMCO:CONNECTED") == 0) handle_client(client);
        stop_motor(); /* fail safe on disconnection */
        close(client);
        printf("Android disconnected; motor stopped.\n");
    }
    close(fd);
    server_fd = -1;
    return NULL;
}

static void *ir_polling_thread(void *arg) {
    (void)arg;
    int last_state = 1;
    while (run_program) {
        int current = (gpio_mem_ptr[13] & (1U << PIN_IR_SENSOR)) ? 1 : 0;
        if (last_state == 1 && current == 0) {
            pthread_mutex_lock(&ir_lock);
            ++ir_edges;
            pthread_mutex_unlock(&ir_lock);
        }
        last_state = current;
        usleep(1000);
    }
    return NULL;
}

int main(void) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);
    if (E4235_Init() != 0) { fprintf(stderr, "E4235_Init failed\n"); return EXIT_FAILURE; }
    if (as5600_init() == 0) {
        encoder_available = 1;
        printf("AS5600 encoder initialized successfully.\n");
    } else {
        encoder_available = 0;
        printf("AS5600 encoder unavailable. Continuing without encoder.\n");
    }
    unsigned int actual = get_timer_frequency();
    if (actual) timer_freq = actual;
    printf("ARM timer frequency: %u Hz\n", timer_freq);
    int motor_pins[4] = {PIN_A, PIN_B, PIN_C, PIN_D};
    E4235_Select(PIN_ENABLE, 1);
    enable_routine(PIN_ENABLE, 0);
    for (int i = 0; i < 4; ++i) {
        E4235_Select(motor_pins[i], 1);
        E4235_Write(motor_pins[i], 0);
    }
    E4235_Select(PIN_IR_SENSOR, 0);
    E4235_KYBdeblock(1);

    pthread_t ir_thread, bt_thread, encoder_tid;
    if (pthread_create(&ir_thread, NULL, ir_polling_thread, NULL) != 0) {
        fprintf(stderr, "Cannot start IR thread\n");
        E4235_KYBdeblock(0);
        return EXIT_FAILURE;
    }
    if (pthread_create(&bt_thread, NULL, bluetooth_thread, NULL) != 0) {
        fprintf(stderr, "Cannot start Bluetooth thread\n");
        run_program = 0;
        pthread_join(ir_thread, NULL);
        E4235_KYBdeblock(0);
        return EXIT_FAILURE;
    }

    pthread_create(&encoder_tid, NULL, encoder_thread, NULL);
    printf("PARMCO CP3 controller ready (20 RPM, CW, stopped).\n");
    printf("Keyboard: E enable/run toggle, S start/stop, C direction, U/D speed, R RPM, Q quit\n");
    int outputs_enabled = 0;
    unsigned int last_rpm_time = get_current_timer();
    get_and_reset_count();
    while (run_program) {
        char key;
        while (read(STDIN_FILENO, &key, 1) > 0) {
            pthread_mutex_lock(&state_lock);
            switch (key) {
                case 'e': case 'E': case 's': case 'S': running = !running; break;
                case 'c': case 'C': direction = !direction; break;
                case 'u': case 'U':
                    steps_per_sec *= 1.02f;
                    if (steps_per_sec > 200.0f * 100.0f / 60.0f) steps_per_sec = 200.0f * 100.0f / 60.0f;
                    break;
                case 'd': case 'D':
                    steps_per_sec *= 0.98f;
                    if (steps_per_sec < 200.0f * 10.0f / 60.0f) steps_per_sec = 200.0f * 10.0f / 60.0f;
                    break;
                case 'q': case 'Q': run_program = 0; running = 0; break;
                default: break;
            }
            pthread_mutex_unlock(&state_lock);
            if (key == 'r' || key == 'R') {
                unsigned int now = get_current_timer();
                float seconds = (float)(now - last_rpm_time) / (float)timer_freq;
                int edges = get_and_reset_count();
                pthread_mutex_lock(&state_lock);
                float commanded = steps_per_sec * 60.0f / (mode ? 400.0f : 200.0f);
                pthread_mutex_unlock(&state_lock);
                float measured = seconds > 0 ? (edges * 60.0f / (PROPELLER_BLADES * seconds)) : 0.0f;
                printf("Commanded RPM: %.2f | IR RPM: %.2f | Edges: %d | Interval: %.2f s\n",
                       commanded, measured, edges, seconds);
                last_rpm_time = now;
            }
        }
        pthread_mutex_lock(&state_lock);
        int should_run = running;
        int step_direction = direction;
        int step_mode = mode;
        float rate = steps_per_sec;
        pthread_mutex_unlock(&state_lock);

        if (should_run && !outputs_enabled) {
            enable_routine(PIN_ENABLE, 1);
            outputs_enabled = 1;
        } else if (!should_run && outputs_enabled) {
            enable_routine(PIN_ENABLE, 0);
            for (int i = 0; i < 4; ++i) E4235_Write(motor_pins[i], 0);
            outputs_enabled = 0;
        }
        if (should_run) {
            step_routine(step_direction, step_mode, motor_pins);
            unsigned int ticks = (unsigned int)((float)timer_freq / rate);
            step_timing_loop(ticks);
        } else {
            usleep(10000);
        }
    }
    enable_routine(PIN_ENABLE, 0);
    for (int i = 0; i < 4; ++i) E4235_Write(motor_pins[i], 0);
    pthread_join(bt_thread, NULL);
    pthread_join(ir_thread, NULL);
    pthread_join(encoder_tid, NULL);
    E4235_KYBdeblock(0);
    for (int i = 0; i < 4; ++i) E4235_Select(motor_pins[i], 0);
    E4235_Select(PIN_ENABLE, 0);
    as5600_close();
    printf("PARMCO stopped safely.\n");
    return EXIT_SUCCESS;
}
