#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <syslog.h>
#include <netinet/in.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>

#define PORT 9000
#define BACKLOG 10
#define BUFFER_SIZE 1024
#define FILE_PATH "/var/tmp/aesdsocketdata"

int server_fd = -1;

void handle_signal(int signal) {
    syslog(LOG_INFO, "Caught signal %d, exiting", signal);
    
    // Close server socket if open
    if (server_fd != -1) {
        close(server_fd);
    }
    
    // Delete the file
    if (remove(FILE_PATH) == 0) {
        syslog(LOG_INFO, "Deleted file %s", FILE_PATH);
    } else {
        syslog(LOG_ERR, "Failed to delete file %s", FILE_PATH);
    }
    
    closelog();
    exit(0);
}

void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    char *packet = NULL;
    size_t packet_len = 0;

    int file_fd = open(FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (file_fd == -1) {
        syslog(LOG_ERR, "Failed to open file: %s", strerror(errno));
        close(client_fd);
        return;
    }

    while ((bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0)) > 0) {
        char *newline = (char*)memchr(buffer, '\n', bytes_received);
        if (newline != NULL) {
            size_t newline_index = newline - buffer + 1;
            packet = (char*)realloc(packet, packet_len + newline_index);
            if (!packet) {
                syslog(LOG_ERR, "Memory allocation failed");
                perror("realloc failed");
                free(packet);
                close(file_fd);
                close(client_fd);
                return;
            }
            memcpy(packet + packet_len, buffer, newline_index);
            packet_len += newline_index;
            ssize_t bytes_written = write(file_fd, packet, packet_len);

            if (bytes_written == -1) {
                syslog(LOG_ERR, "Write to file failed");
                perror("write failed");
                free(packet);
                close(file_fd);
                close(client_fd);
                return;
            }

            packet_len = 0;

            // Close and reopen the file for reading 
            close(file_fd);
            file_fd = open(FILE_PATH, O_RDONLY);

            if (file_fd == -1) {
                syslog(LOG_ERR, "File open failed");
                perror("file open failed");
                free(packet);
                close(client_fd);
                return;
            }

            // Read the file contents and send them to the client 
            char read_buffer[1024];
            ssize_t bytes_read;
            while ((bytes_read = read(file_fd, read_buffer, sizeof(read_buffer))) > 0) {
                send(client_fd, read_buffer, bytes_read, 0);
            }
            close(file_fd); // Close after reading
            break; // Exit the loop after sending the content
        } else {
            packet = (char*)realloc(packet, packet_len + bytes_received);
            if (!packet) {
                syslog(LOG_ERR, "Memory allocation failed");
                perror("realloc failed");
                free(packet);
                close(file_fd);
                close(client_fd);
                return;
            }
            memcpy(packet + packet_len, buffer, bytes_received);
            packet_len += bytes_received;
        }
    }

    free(packet);
    close(file_fd);
    close(client_fd);
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        // Parent process
        exit(EXIT_SUCCESS);
    }

    // Child process
    if (setsid() < 0) {
        perror("setsid failed");
        exit(EXIT_FAILURE);
    }

    // Redirect standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    open("/dev/null", O_RDONLY);
    open("/dev/null", O_RDWR);
    open("/dev/null", O_RDWR);
}

int main(int argc, char *argv[]) {
    bool daemon_mode = false;

    // Parse command-line arguments
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = true;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    // Register signal handlers
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    server_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "Failed to create socket: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Failed to bind socket: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, BACKLOG) == -1) {
        syslog(LOG_ERR, "Failed to listen on socket: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    if (daemon_mode) {
        daemonize();
    }

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_fd == -1) {
            if (errno == EINTR) {
                // If the accept call was interrupted by a signal, continue
                continue;
            }
            syslog(LOG_ERR, "Failed to accept connection: %s", strerror(errno));
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        handle_client(client_fd);
    }

    close(server_fd);
    closelog();
    return 0;
}

