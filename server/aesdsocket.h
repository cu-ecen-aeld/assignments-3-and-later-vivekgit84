#include <netinet/in.h>

int server_fd = -1;
char client_ip[INET_ADDRSTRLEN];

void clean(char *packet, int file_fd, int client_fd);
int read_file_and_send(char *packet, int client_fd);
void handle_signal(int signal);
void* handle_client(void* arg);
void daemonize();
