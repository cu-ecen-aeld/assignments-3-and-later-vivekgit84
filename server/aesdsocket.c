#include "aesdsocket.h"
#include <sys/queue.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#define PORT 9000
#define BACKLOG 10
#define BUFFER_SIZE 1024
#define FILE_PATH "/var/tmp/aesdsocketdata"

pthread_mutex_t thread_list_mutex  = PTHREAD_MUTEX_INITIALIZER;
#define TIMER_INTERVAL_SEC 10

typedef struct thread_node{
    pthread_t thread_id;
    bool iscomplete;
    SLIST_ENTRY(thread_node) next;
}thread_node;

void handle_timer() {
    time_t now = time(NULL);
    struct tm* local_time = localtime(&now);
    char timestamp[256];
    strftime(timestamp, sizeof(timestamp), "timestamp: %a, %d %b %Y %H:%M:%S %z\n", local_time);

    pthread_mutex_lock(&thread_list_mutex);
    int file_fd = open(FILE_PATH, O_WRONLY | O_APPEND, 0644);
    if (file_fd == -1) {
        syslog(LOG_ERR, "Failed to open file: %s", strerror(errno));
    } else {
        write(file_fd, timestamp, strlen(timestamp));
        close(file_fd);
    }
    pthread_mutex_unlock(&thread_list_mutex);
}

void setup_timer() {
    struct sigaction sa;
    struct sigevent sev;
    struct itimerspec its;
    timer_t timer_id;

    // Set up the signal handler for the timer
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_timer;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGRTMIN, &sa, NULL) == -1) {
        syslog(LOG_ERR, "Failed to set up signal handler: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Set up the timer
    memset(&sev, 0, sizeof(sev));
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGRTMIN;
    if (timer_create(CLOCK_REALTIME, &sev, &timer_id) == -1) {
        syslog(LOG_ERR, "Failed to create timer: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Set the timer interval
    its.it_value.tv_sec = TIMER_INTERVAL_SEC;
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = TIMER_INTERVAL_SEC;
    its.it_interval.tv_nsec = 0;
    if (timer_settime(timer_id, 0, &its, NULL) == -1) {
        syslog(LOG_ERR, "Failed to set timer: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

// Define the head of the singly linked list using queue.h macros
SLIST_HEAD(slisthead, thread_node) head;


void handle_signal(int signal) {
    syslog(LOG_INFO, "Caught signal %d, exiting", signal);
    printf("Caught signal %d, exiting\n", signal);
    
     struct thread_node *node;
    
    SLIST_FOREACH(node, &head, next) 
    {
    	pthread_cancel(node->thread_id);
    }
    
    SLIST_FOREACH(node, &head, next) 
    {
       pthread_join(node->thread_id, NULL);
      
      SLIST_REMOVE(&head, node, thread_node, next);  // Remove the node from the list
      free(node);  // Free the node
    }
    
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

void clean(char *packet, int file_fd, int client_fd)
{
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
    free(packet);
    close(file_fd);
    close(client_fd);	
}

void* handle_client(void* arg) {
    pthread_mutex_lock(&thread_list_mutex ); 
    int client_fd = *(int*)arg;
    free(arg);
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    char *packet = NULL;
    size_t packet_len = 0;

    int file_fd = open(FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (file_fd == -1) {
        syslog(LOG_ERR, "Failed to open file: %s", strerror(errno));
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        close(client_fd);
        return NULL;
    }

    while ((bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0)) > 0) {
        char *newline = (char*)memchr(buffer, '\n', bytes_received);
        if (newline != NULL) {
            size_t newline_index = newline - buffer + 1;
            packet = (char*)realloc(packet, packet_len + newline_index);
            if (!packet) {
                syslog(LOG_ERR, "Memory allocation failed");
                perror("realloc failed");
                clean(packet, file_fd, client_fd);
                return NULL;
            }
            memcpy(packet + packet_len, buffer, newline_index);
            packet_len += newline_index;
            ssize_t bytes_written = write(file_fd, packet, packet_len);

            if (bytes_written == -1) {
                syslog(LOG_ERR, "Write to file failed");
                perror("write failed");
                clean(packet, file_fd, client_fd);
                return NULL;
            }

            packet_len = 0;

            // Close and reopen the file for reading 
            close(file_fd);
            file_fd = read_file_and_send(packet, client_fd);
            if(file_fd == -1)
            {
            	return NULL;
            }
         
            break; // Exit the loop after read and sending the content
        } else {
            packet = (char*)realloc(packet, packet_len + bytes_received);
            if (!packet) {
                syslog(LOG_ERR, "Memory allocation failed");
                perror("realloc failed");
                clean(packet, file_fd, client_fd);
                return NULL;
            }
            memcpy(packet + packet_len, buffer, bytes_received);
            packet_len += bytes_received;
        }
    }

    clean(packet, file_fd, client_fd);
    
    pthread_t threadId = pthread_self();
    
     struct thread_node *node;
    
    // Iterate through the singly linked list and print the data
    SLIST_FOREACH(node, &head, next) 
    {
	if(node->thread_id == threadId)
	{
	   node->iscomplete = true;
	}
    }
           
    
    pthread_mutex_unlock(&thread_list_mutex );
    
     return NULL;
}

int read_file_and_send(char *packet, int client_fd)
{
	int file_fd = open(FILE_PATH, O_RDONLY);
	if (file_fd == -1) {
		syslog(LOG_INFO, "Closed connection from %s", client_ip);
		syslog(LOG_ERR, "File open failed");
		perror("file open failed");
		free(packet);
		close(client_fd);
		return file_fd;
	}

	// Read the file contents and send them to the client 
	char read_buffer[1024];
	ssize_t bytes_read;
	while ((bytes_read = read(file_fd, read_buffer, sizeof(read_buffer))) > 0) {
	send(client_fd, read_buffer, bytes_read, 0);
	}
	
	return file_fd;
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

void add_thread_in_list(pthread_t thread_id)
{
	pthread_mutex_lock(&thread_list_mutex);

	thread_node* node = (thread_node*)malloc(sizeof(thread_node));
	if (node == NULL) {
	    perror("malloc");
	    exit(EXIT_FAILURE);
	}
	
	node->thread_id = thread_id;
        node->iscomplete = false;
        SLIST_INSERT_HEAD(&head, node, next); // insert node at next of head
        pthread_mutex_unlock(&thread_list_mutex);
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
    
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
    syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));
    close(server_fd);
    closelog();
    exit(EXIT_FAILURE);
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
    
    // Initialize the head of the list
    SLIST_INIT(&head);
    
    // Setup the timer
    setup_timer();

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

        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);
        
        // Create a new thread to handle the client connection
        pthread_t thread_id;
        int *client_fd_ptr = malloc(sizeof(int));
        *client_fd_ptr = client_fd;
        pthread_create(&thread_id, NULL, handle_client, client_fd_ptr);
        
        add_thread_in_list(thread_id);
        
         struct thread_node *node;
        
        // Iterate through the singly linked list
	SLIST_FOREACH(node, &head, next)
	{
		if(node->iscomplete)
		{
		   pthread_join(node->thread_id, NULL);
		   SLIST_REMOVE(&head, node, thread_node, next);  // Remove the node from the list
		   free(node);  // Free the node
		}
        }
             
        
    }

    return 0;
}

