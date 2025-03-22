#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>

#define PORT 9000

#define USE_AESD_CHAR_DEVICE

#ifdef USE_AESD_CHAR_DEVICE
#define DATA_FILE "/dev/aesdchar"
#else
#define DATA_FILE "/var/tmp/aesdsocketdata"
#endif

typedef struct thread_node {
    pthread_t tid;
    struct thread_node *next;
} ThreadNode;

int server_fd;
pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
ThreadNode *thread_list = NULL;                    // Linked list to manage threads

// Signal handler to gracefully exit the program
void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");

        pthread_mutex_lock(&data_mutex);  // Lock mutex before cleanup

        // Request exit from each thread and wait for threads to complete execution
        ThreadNode *current = thread_list;
        while (current != NULL) {
            pthread_cancel(current->tid);  // Request thread exit
            pthread_join(current->tid, NULL);  // Wait for thread to complete
            ThreadNode *temp = current;
            current = current->next;
            free(temp);  // Free thread node memory
        }
        thread_list = NULL;

        pthread_mutex_unlock(&data_mutex);  // Unlock mutex after cleanup

        close(server_fd);  // Close server socket
        #ifndef USE_AESD_CHAR_DEVICE
        remove(DATA_FILE); // Remove data file
        #endif
        exit(EXIT_SUCCESS); // Exit the program
    }
}

// Function to handle sending data back to the client
void send_data_to_client(int client_socket) {
    FILE *fp = fopen(DATA_FILE, "r");
    if (fp == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        send(client_socket, buffer, strlen(buffer), 0);
    }

    fclose(fp);
}
    #ifndef USE_AESD_CHAR_DEVICE
void *append_timestamp(void *arg) {
    // Function to append timestamp every 10 seconds
    while (1) {
        time_t current_time = time(NULL);
        struct tm *time_info = localtime(&current_time);

        char timestamp[100];
        strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %H:%M:%S %z", time_info);
        strcat(timestamp, "\n");

        pthread_mutex_lock(&data_mutex); // Lock mutex for file writing

        FILE *fp = fopen(DATA_FILE, "a");
        if (fp == NULL) {
            perror("fopen");
            pthread_mutex_unlock(&data_mutex);
            sleep(10);
            continue;
        }

        fputs(timestamp, fp);
        fclose(fp);

        pthread_mutex_unlock(&data_mutex); // Unlock mutex after file writing

        sleep(10);
    }
}
    #endif
// Function executed by each thread to handle client connection
void *connection_handler(void *socket_desc) {
    int client_socket = *(int *)socket_desc;  // Get client socket from argument
    free(socket_desc);  // Free the allocated memory for socket descriptor

    char buffer[1024];
    ssize_t bytes_received;
    FILE *fp = fopen(DATA_FILE, "a+");
    if (fp == NULL) {
        perror("fopen");
        pthread_exit(NULL);  // Exit thread on file open error
    }

    while ((bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
        pthread_mutex_lock(&data_mutex);  // Lock mutex before critical section

        buffer[bytes_received] = '\0';
        fprintf(fp, "%s", buffer);  // Write received data to file

        char *newline = strchr(buffer, '\n');
        if (newline != NULL) {
            fflush(fp);  // Flush data to file
            send_data_to_client(client_socket);  // Send data back to client
            memset(buffer, 0, sizeof(buffer));  // Clear buffer for next data
        }

        pthread_mutex_unlock(&data_mutex);  // Unlock mutex after critical section
    }

    close(client_socket);  // Close client socket
    fclose(fp);             // Close file
    pthread_exit(NULL);     // Exit thread
}

int main(int argc, char *argv[]) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler; // Set signal handler function
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);   // Register SIGINT signal handler
    sigaction(SIGTERM, &sa, NULL);  // Register SIGTERM signal handler

    int daemon_mode = 0;

    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;  // Enable daemon mode if "-d" argument provided
    }

    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("Fork failed");
            exit(EXIT_FAILURE);
        }
        if (pid > 0) {
            exit(EXIT_SUCCESS); // Parent process exits
        }
        umask(0);
        if (setsid() < 0) {
            perror("Setsid failed");
            exit(EXIT_FAILURE);
        }
    }

    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }


    #ifndef USE_AESD_CHAR_DEVICE
        pthread_t timestamp_thread;
    if (pthread_create(&timestamp_thread, NULL, append_timestamp, NULL) != 0) {
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }
    #endif

    while (1) {
        int *new_socket = malloc(sizeof(int));  // Allocate memory for new socket
        if (new_socket == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }

        *new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (*new_socket == -1) {
            perror("accept");
            free(new_socket);
            exit(EXIT_FAILURE);
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, connection_handler, (void *)new_socket) != 0) {
            perror("pthread_create");
            free(new_socket);
            exit(EXIT_FAILURE);
        }

        // Create a new node for the linked list to store thread information
        ThreadNode *new_node = (ThreadNode *)malloc(sizeof(ThreadNode));
        if (new_node == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        new_node->tid = tid;
        new_node->next = NULL;

        pthread_mutex_lock(&data_mutex);  // Lock mutex before adding to linked list

        // Add the new thread node to the linked list
        if (thread_list == NULL) {
            thread_list = new_node;
        } else {
            ThreadNode *current = thread_list;
            while (current->next != NULL) {
                current = current->next;
            }
            current->next = new_node;
        }

        pthread_mutex_unlock(&data_mutex);  // Unlock mutex after adding to linked list
    }

    return 0;
}
