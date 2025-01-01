#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <syslog.h>
#include <netinet/in.h>

#define PORT 9000
#define BACKLOG 10
#define BUFFER_SIZE 1024
#define FILE_PATH "/var/tmp/aesdsocketdata"

void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    int file_fd = open(FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (file_fd == -1) {
        syslog(LOG_ERR, "Failed to open file: %s", strerror(errno));
        close(client_fd);
        return;
    }

    while ((bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0)) > 0) {
        if (write(file_fd, buffer, bytes_received) == -1) {
            syslog(LOG_ERR, "Failed to write to file: %s", strerror(errno));
            close(file_fd);
            close(client_fd);
            return;
        }
    }

    close(file_fd);
    close(client_fd);
}

int main() {
    openlog("aesdsocket", LOG_PID, LOG_USER);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
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

    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_fd == -1) {
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
