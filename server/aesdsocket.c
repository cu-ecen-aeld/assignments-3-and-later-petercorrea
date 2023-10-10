#include <arpa/inet.h>
#include <fcntl.h>
#include <getopt.h>
#include <malloc.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#define BUFFER_SIZE 1000000
#define PORT 9000
int sockfd;
int file_fd;

// Close the file, socket, syslog, cleanup and exit
void sig_handler(int signum) {
  (void)signum;
  syslog(LOG_INFO, "Caught signal, exiting");
  close(file_fd);
  close(sockfd);
  unlink("/var/tmp/aesdsocketdata");
  closelog();
  exit(EXIT_SUCCESS);
}

// fork, update file permissions, open new sess and close standard file
// descriptors
void create_daemon() {
  pid_t pid = fork(), sid;
  if (pid < 0)
    exit(EXIT_FAILURE);
  if (pid > 0)
    exit(EXIT_SUCCESS); // parent process exits, leaving child
  umask(0);
  if ((sid = setsid()) < 0) // isolate the child process from the parent's
                            // session and process group
    exit(EXIT_FAILURE);
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
}

void process_client(int client_sock, struct sockaddr_in client_addr) {
  // IP address buffer
  char client_ip[INET_ADDRSTRLEN], *buffer = malloc(BUFFER_SIZE);
  memset(buffer, 0, BUFFER_SIZE);

  // Convert the client's IP address from binary to text and log
  inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
  syslog(LOG_INFO, "Accepted connection from %s", client_ip);

  // Open the data file for writing, create if not exists
  file_fd =
      open("/var/tmp/aesdsocketdata", O_WRONLY | O_APPEND | O_CREAT, 0666);
  if (file_fd == -1) {
    perror("open");
    close(client_sock);
    free(buffer);
    return;
  }

  // Receive data from the client, until newline is found or buffer is full
  ssize_t bytes_received, total_received = 0;
  while ((bytes_received = recv(client_sock, buffer, BUFFER_SIZE - 1, 0)) > 0) {
    // Check if buffer exceeds heap
    if (total_received > mallinfo().fordblks) {
      syslog(LOG_INFO, "Buffer exceeds heap, closing socket");
      close(file_fd);
      close(client_sock);
      free(buffer);
      return;
    }

    if (strchr(buffer, '\n') != NULL)
      break;

    total_received += bytes_received;
  }

  write(file_fd, buffer, bytes_received);
  close(file_fd);

  // Echo back the received data to the client
  file_fd = open("/var/tmp/aesdsocketdata", O_RDONLY);
  if (file_fd == -1) {
    perror("open");
    close(client_sock);
    free(buffer);
    return;
  }

  // Read from file and send to client
  while ((bytes_received = read(file_fd, buffer, sizeof(buffer))) > 0) {
    send(client_sock, buffer, bytes_received, 0);
  }

  memset(buffer, 0, BUFFER_SIZE * sizeof(char));
  close(file_fd);
  free(buffer);

  syslog(LOG_INFO, "Closed connection from %s", client_ip);
  close(client_sock);
}

int main(int argc, char *argv[]) {
  int client_sock, port = PORT;
  struct sockaddr_in server_addr, client_addr;

  openlog("SOCKET_SERVER", LOG_CONS | LOG_PID, LOG_USER);

  bool daemon_mode = false;

  // Parse command-line arguments
  int opt;
  while ((opt = getopt(argc, argv, "d")) != -1) {
    switch (opt) {
    case 'd':
      daemon_mode = true;
      break;
    default:
      fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
      exit(EXIT_FAILURE);
    }
  }

  // Daemonize if in daemon mode
  if (daemon_mode) {
    create_daemon();
  }

  // Create a socket
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  // Enable address reuse for socket
  int enable = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
    perror("setsockopt(SO_REUSEADDR) failed");
  }

  // Set up the server address structure
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);

  // Bind socket to port
  if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) ==
      -1) {
    perror("bind");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  // Listen for incoming connections
  if (listen(sockfd, 5) == -1) {
    perror("listen");
    close(sockfd);
    exit(EXIT_FAILURE);
  }

  socklen_t client_addr_len = sizeof(client_addr);

  // Set up the signal handlers
  signal(SIGINT, sig_handler);
  signal(SIGTERM, sig_handler);

  // Main loop to accept and process client connections
  while (1) {
    client_sock =
        accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (client_sock == -1) {
      perror("accept");
      continue;
    }

    process_client(client_sock, client_addr);
  }

  return 0;
}
