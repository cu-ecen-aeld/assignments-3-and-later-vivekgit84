#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <netinet/in.h>

#define PORT 9000
#define BACKLOG 10
#define BUFFER_SIZE 1024
#define FILE_PATH "/var/tmp/aesdsocketdata"



#include <sys/stat.h>

#define MAX_PACKAGE_LEN 1024*100  // 100 Kbytes

static fd_set active_fds;
static char persistent_file[] = "/var/tmp/aesdsocketdata";

static ssize_t read_from_file(const char* user_file, char* read_buff) {

    /**
     * Read file content
     * @param user_file Write to read from
     * @param str The memory to store the file content
     * @return  Return the number of bytes read, or -1 if an error occure
     */

    FILE *fptr;
    size_t sz;
    long int file_sz;
    
    // Open read only
    fptr = fopen(user_file, "r");
    if (fptr == NULL) {
        printf("Failed to open %s.\n", user_file);
        return -1;
    }

    // Check file permission
    struct stat st;
    stat(user_file, &st);
    printf("File permissions:  %04o.\n", st.st_mode & 0777);

    // Read file content
    fseek(fptr, 0, SEEK_END); // Move file position to the end of the file
    file_sz = ftell(fptr); // Get the current file position
    fseek(fptr, 0, SEEK_SET); // Reset file position to start of file
    sz = fread(read_buff, 1, file_sz, fptr);
    if ((long int)sz != file_sz) {
        printf("Warning: read %ld bytes != %ld file size.\n", sz, file_sz);
    }
    printf("Read '%ld' bytes from file '%s'.\n", sz, user_file);

    // Close the file
    if (fclose(fptr) < 0) {
        printf("Failed to close %s.\n", user_file);
        return -1;
    }

    return sz;
}

static int write_to_file(const char* user_file, const char* str) {

    /**
     * Log messge to file
     * @param user_file File to write to 
     * @param str The message to log
     * @return Return the number of bytes written, or -1 if an error occure
     */

    int fptr;
    int sz;
    
    // Append to already created file
    fptr = open(user_file, O_WRONLY | O_APPEND | O_CREAT, 0600);

    if (fptr == -1) {
        printf("Failed to open %s.\n", user_file);
        return -1;
    }

    // Write the buffer to the file
    sz = write(fptr, str, strlen(str));
    printf("Wrote '%d' bytes to file '%s'.\n", sz, user_file);
    if (sz == -1) {
        printf("Failed to write to file.\n");
        return -1;
    }

    // Close the file
    if (close(fptr) < 0) {
        printf("Failed to close %s.\n", user_file);
        return -1;
    }

    return sz;
}


static void msg_exchange(int connfd) { 
    /**
     * Function designed for msg exchange between client and server. 
     * @param connfd The socket connection to client with client_ip
     * @param clientip The ip of the socket client
     * @return Void
     */
    
    char buff[MAX_PACKAGE_LEN+1]; // One byte padding for '\0' termination
    ssize_t buff_len = 0;
    char buff_offset = 0;
    bzero(buff, MAX_PACKAGE_LEN+1); 
    char *read_buff = (char*)malloc(MAX_PACKAGE_LEN);
    ssize_t read_buff_total = 0;
    ssize_t send_buff_len = 0;
    ssize_t send_buff_total = 0;

    // Read the message from client non blocking and copy it in buffer 
    buff_len = recv(connfd, (buff + buff_offset), sizeof(buff), MSG_DONTWAIT); 
    if (buff_len == -1 && errno == EAGAIN) {
        printf("Waiting for incoming data from client fd %d.\n", connfd); 
    } else if (buff_len == 0) {
        FD_CLR(connfd, &active_fds);
        
  
    } else if (buff_len > MAX_PACKAGE_LEN) {
        printf("Packet from client fd %d exeeds length of %d bytes: Discarded.\n", connfd, (int)MAX_PACKAGE_LEN); 
    } else {
        if (buff_len > 0) {
            // Check string termination ('\0')
            if (strlen(buff) != (size_t)(buff_len-1)) {
                buff[buff_len] = '\0'; // padding string termination
            }

            // Check package termination (newline)
            if (buff[strlen(buff)-1] == '\n') {
                printf("Received package from client fd %d: %s", connfd, buff); 

                // Write package to persistance file
                if (write_to_file(persistent_file, buff) == -1) {
                    printf("Failed to log message to persistant file.\n");
                } else {
                    // Send all packages to the client
                    read_buff_total = read_from_file(persistent_file, read_buff);

                    if (read_buff_total != -1) {
                        while(send_buff_total < read_buff_total) { 
                            printf("Sending all packages to client fd %d.\n", connfd);
                            //printf("Packages:\n%s", read_buff);  
                            send_buff_len = send(connfd,read_buff, read_buff_total, MSG_DONTWAIT);
                            if (buff_len == -1 && errno == EAGAIN) {
                                continue;
                            }
                            send_buff_total += send_buff_len; 
                        }
                        printf("%ld bytes sent.\n", send_buff_total); 
                    } else {
                        printf("Failed to read all packages from persistant file.\n");
                    }
                }
            } else {
                printf("Missing package termination (\n) from client fd %d.\n", connfd); 
            }
        }
    }

    free(read_buff);
} 

int server_fd = -1;

void handle_signal(int signal) {
    syslog(LOG_INFO, "Caught signal %d, exiting", signal);
    printf("Caught signal %d, exiting\n", signal);
    
    // Close server socket if open
    if (server_fd != -1) {
    	shutdown(server_fd, SHUT_RDWR);
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
        exit(1);
    }
    if (pid > 0) {
        // Parent process
        exit(0);
    }

    // Child process
    if (setsid() < 0) {
        perror("setsid failed");
        exit(1);
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
    int daemon_mode = 0;

    // Parse command-line arguments
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    struct sigaction sa; 
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_handler = handle_signal; 
    
    if (sigaction(SIGINT, &sa, NULL) != 0) 
    { 
    	perror("Error handling SIGINT"); 
    	exit(1); 
    } 
    
    if (sigaction(SIGTERM, &sa, NULL) != 0) 
    { 
	perror("Error handling SIGTERM"); 
	exit(1);
    }
    	

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

    while (1) {
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

        /*handle_client(client_fd);*/
        msg_exchange(client_fd);
    }

    close(server_fd);
    closelog();
    return 0;
}

