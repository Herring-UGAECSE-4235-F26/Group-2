/**Need to run these commands on the pi sudo apt update
sudo apt install bluez libbluetooth-dev */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>

#define RFCOMM_CHANNEL 1
#define MAX_COMMAND 128

static volatile sig_atomic_t running = 1;

void handle_signal(int signal_number)
{
    (void)signal_number;
    running = 0;
}

/*
 * Send a newline-terminated message to the Android app.
 * Returns 0 on success and -1 on failure.
 */
int send_message(int socket_fd, const char *message)
{
    char buffer[256];
    int length = snprintf(buffer, sizeof(buffer), "%s\n", message);

    if (length < 0 || length >= (int)sizeof(buffer)) {
        return -1;
    }

    int sent = 0;

    while (sent < length) {
        ssize_t result = send(socket_fd,
                              buffer + sent,
                              length - sent,
                              MSG_NOSIGNAL);

        if (result < 0 && errno == EINTR) {
            continue;
        }

        if (result <= 0) {
            return -1;
        }

        sent += result;
    }

    return 0;
}

/*
 * Recognize the commands received from MIT App Inventor.
 * CP2: print the commands only; do not operate the motor.
 */

/*
 * Recognize commands received from MIT App Inventor.
 * CP2: acknowledge commands without controlling the motor.
 */
int process_command(int client_fd, const char *command)
{
    char response[160];

    printf("Received: %s\n", command);
    fflush(stdout);

    if (strcmp(command, "START") == 0 ||
        strcmp(command, "STOP") == 0 ||
        strcmp(command, "DIR:CW") == 0 ||
        strcmp(command, "DIR:CCW") == 0 ||
        strcmp(command, "FASTER") == 0 ||
        strcmp(command, "SLOWER") == 0 ||
        strcmp(command, "CALIBRATE_ZERO") == 0 ||
        strcmp(command, "SET_ZERO") == 0 ||
        strcmp(command, "MAINTAIN:ON") == 0 ||
        strcmp(command, "MAINTAIN:OFF") == 0 ||
        strcmp(command, "LOG:START") == 0 ||
        strcmp(command, "LOG:STOP") == 0) {

        snprintf(response, sizeof(response),
                 "ACK:%s", command);

    } else if (strncmp(command, "RPM:", 4) == 0 ||
               strncmp(command, "ANGLE:", 6) == 0) {

        // CP2: Accept formatted commands without applying them.
        snprintf(response, sizeof(response),
                 "ACK:%s", command);

    } else {
        snprintf(response, sizeof(response),
                 "ERR:UNKNOWN_COMMAND");
    }

    printf("Sending: %s\n", response);
    fflush(stdout);

    return send_message(client_fd, response);
}


/*
 * Read the Bluetooth byte stream and assemble commands
 * terminated by newline (\n). Also accept CRLF (\r\n).
 */
void handle_client(int client_fd)
{
    char command[MAX_COMMAND];
    size_t position = 0;
    int overflow = 0;
    char buffer[256];

    while (running) {
        ssize_t received = recv(client_fd, buffer, sizeof(buffer), 0);

        if (received == 0) {
            printf("Android disconnected.\n");
            break;
        }

        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("Bluetooth receive");
            break;
        }

        for (ssize_t i = 0; i < received; i++) {
            char c = buffer[i];

            if (c == '\n') {
                if (overflow) {
                    send_message(client_fd, "ERR:COMMAND_TOO_LONG");
                } else if (position > 0) {
                    command[position] = '\0';

                    if (process_command(client_fd, command) < 0) {
                        perror("Bluetooth send");
                        return;
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
}

int main(void)
{
    int server_fd;
    struct sockaddr_rc local_address = {0};

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    server_fd = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);

    if (server_fd < 0) {
        perror("Bluetooth socket");
        return EXIT_FAILURE;
    }

    local_address.rc_family = AF_BLUETOOTH;
    bacpy(&local_address.rc_bdaddr, BDADDR_ANY);
    local_address.rc_channel = RFCOMM_CHANNEL;

    if (bind(server_fd,
             (struct sockaddr *)&local_address,
             sizeof(local_address)) < 0) {
        perror("Bluetooth bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, 1) < 0) {
        perror("Bluetooth listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("PARMCO Bluetooth Server\n");
    printf("Listening on RFCOMM channel %d\n", RFCOMM_CHANNEL);
    printf("Waiting for Android connection...\n");
    fflush(stdout);

    while (running) {
        struct sockaddr_rc remote_address = {0};
        socklen_t address_length = sizeof(remote_address);

        int client_fd = accept(
            server_fd,
            (struct sockaddr *)&remote_address,
            &address_length
        );

        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("Bluetooth accept");
            break;
        }

        char address[18] = {0};
        ba2str(&remote_address.rc_bdaddr, address);

        printf("\nAndroid connected: %s\n", address);
        fflush(stdout);

        if (send_message(client_fd, "PARMCO:CONNECTED") == 0) {
            handle_client(client_fd);
        }

        close(client_fd);

        if (running) {
            printf("Waiting for Android reconnection...\n");
            fflush(stdout);
        }
    }

    close(server_fd);
    return EXIT_SUCCESS;
}
