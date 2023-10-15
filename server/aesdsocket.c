#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define PORT "9000"
#define BACKLOG 10
#define FILE_NAME "/var/tmp/aesdsocketdata"

bool caught_sigint = false;
bool caught_sigterm = false;

pthread_mutex_t read_write_mutex;

struct node {
  struct data {
    int new_fd;
    char ip_str[INET_ADDRSTRLEN];
    pthread_t *thread;
  } *data;
  SLIST_ENTRY(node) nodes;
};

static void signal_handler(int signal_number) {
  switch (signal_number) {
  case SIGINT:
    caught_sigint = true;
    break;
  case SIGTERM:
    caught_sigterm = true;
    break;
  default:
    break;
  }
}

static void get_current_time_string(char *time_string, size_t size) {
  time_t t = time(NULL);
  struct tm *tmp = localtime(&t);

  if (strftime(time_string, size, "timestamp:%a, %d %b %Y %T %z\n", tmp) == 0) {
    if (errno != EINTR) {
      syslog(LOG_ERR,
             "Error setting time_string with strftime from timer_thread: %s",
             strerror(errno));
    }
  }
}

static void write_to_file(const char *time_string) {
  FILE *file_to_write = fopen(FILE_NAME, "a+");
  fwrite(time_string, sizeof time_string[0], strlen(time_string),
         file_to_write);
  fclose(file_to_write);
}

static void timer_thread(union sigval sigval) {
  char time_string[1024];

  get_current_time_string(time_string, sizeof(time_string));

  if (pthread_mutex_lock(&read_write_mutex) != 0) {
    if (errno != EINTR) {
      syslog(LOG_ERR, "Error %d (%s) locking thread data!", errno,
             strerror(errno));
      return;
    }
  }

  write_to_file(time_string);

  if (pthread_mutex_unlock(&read_write_mutex) != 0) {
    if (errno != EINTR) {
      syslog(LOG_ERR, "Error %d (%s) unlocking thread data!", errno,
             strerror(errno));
    }
  }
}

static bool configure_itimerspec(struct itimerspec *itimerspec,
                                 unsigned int timer_period,
                                 struct timespec *start_time) {
  memset(itimerspec, 0, sizeof(struct itimerspec));
  itimerspec->it_interval.tv_sec = timer_period;
  itimerspec->it_interval.tv_nsec = 0;
  itimerspec->it_value.tv_sec =
      start_time->tv_sec + itimerspec->it_interval.tv_sec;
  return true;
}

static bool setup_timer(int clock_id, timer_t timerid,
                        unsigned int timer_period,
                        struct timespec *start_time) {
  bool success = false;

  if (timer_period == 0) {
    syslog(LOG_ERR, "Unsupported timer period (must be non-zero)");
    return success;
  }

  if (clock_gettime(clock_id, start_time) != 0) {
    syslog(LOG_ERR, "Error %d (%s) getting clock %d time\n", errno,
           strerror(errno), clock_id);
    return success;
  }

  struct itimerspec itimerspec;
  if (!configure_itimerspec(&itimerspec, timer_period, start_time)) {
    syslog(LOG_ERR, "Error configuring itimerspec");
    return success;
  }

  if (timer_settime(timerid, TIMER_ABSTIME, &itimerspec, NULL) != 0) {
    syslog(LOG_ERR, "Error %d (%s) setting timer\n", errno, strerror(errno));
    return success;
  }

  success = true;
  return success;
}

static void handle_error_and_exit(const char *msg, int err_val, char *buf,
                                  void *thread_param) {
  syslog(LOG_ERR, "%s: %s", msg, strerror(err_val));
  if (buf)
    free(buf);
  pthread_exit(thread_param);
}

static bool try_lock_mutex(int *rc, char *buf, void *thread_param) {
  *rc = pthread_mutex_lock(&read_write_mutex);
  if (*rc != 0 && errno != EINTR) {
    handle_error_and_exit("Error locking mutex for read_write_thread", errno,
                          buf, thread_param);
    return false;
  }
  return true;
}

static bool try_unlock_mutex(int *rc, char *buf, void *thread_param) {
  *rc = pthread_mutex_unlock(&read_write_mutex);
  if (*rc != 0 && errno != EINTR) {
    handle_error_and_exit("Error unlocking mutex for read_write_thread", errno,
                          buf, thread_param);
    return false;
  }
  return true;
}

void *read_write_thread(void *thread_param) {
  int buf_size = 1024;
  char *buf = malloc(buf_size * sizeof(char));
  int rc, byte_count;

  struct data *thread_args = (struct data *)thread_param;

  syslog(LOG_DEBUG, "Accepted connection to %s\n", thread_args->ip_str);

  if (!try_lock_mutex(&rc, buf, thread_param)) {
    return thread_param;
  }

  FILE *file_to_write = fopen(FILE_NAME, "a+");
  if (!file_to_write && errno != EINTR) {
    handle_error_and_exit("Error opening /var/tmp/aesdsocketdata", errno, buf,
                          thread_param);
  }

  byte_count = recv(thread_args->new_fd, buf, buf_size, 0);
  while (byte_count == buf_size) {
    fwrite(buf, sizeof buf[0], byte_count, file_to_write);
    byte_count = recv(thread_args->new_fd, buf, buf_size, 0);
  }
  fwrite(buf, sizeof buf[0], byte_count, file_to_write);

  rewind(file_to_write);
  byte_count = fread(buf, sizeof buf[0], buf_size, file_to_write);
  send(thread_args->new_fd, buf, byte_count, MSG_MORE);
  while (byte_count == buf_size) {
    byte_count = fread(buf, sizeof buf[0], buf_size, file_to_write);
    send(thread_args->new_fd, buf, byte_count, MSG_MORE);
  }

  if (!try_unlock_mutex(&rc, buf, thread_param)) {
    return thread_param;
  }

  shutdown(thread_args->new_fd, 2);
  syslog(LOG_DEBUG, "Closed connection to %s\n", thread_args->ip_str);
  fclose(file_to_write);
  free(buf);

  return thread_param;
}

int send_and_receive(void) {
  int sockfd;
  struct addrinfo hints, *res;

  int new_fd, err_val;
  struct sockaddr_in their_addr;
  socklen_t sin = sizeof their_addr;

  SLIST_HEAD(head_s, node) head;
  SLIST_INIT(&head);

  struct node *new_node = NULL;
  struct data *new_data = NULL;
  pthread_t *new_thread = NULL;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  timer_t timerid;
  struct sigevent sev;
  memset(&sev, 0, sizeof(struct sigevent));
  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_notify_function = timer_thread;
  int yes = 1;

  getaddrinfo(NULL, PORT, &hints, &res);
  syslog(LOG_DEBUG, "Attempting to create a socket file descriptor");
  sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sockfd == -1) {
    int err_val = errno;
    if (errno != EINTR) {
      syslog(LOG_ERR, "Error creating a socket file descriptor: %s",
             strerror(err_val));
      freeaddrinfo(res);
      closelog();
      return -1;
    }
  }
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1) {
    int err_val = errno;
    if (errno != EINTR) {
      syslog(LOG_ERR, "Error adjusting socket file descriptor options: %s",
             strerror(err_val));
      freeaddrinfo(res);
      closelog();
      return -1;
    }
    exit(1);
  }
  syslog(LOG_DEBUG, "Attempting to bind to socket file descriptor");
  if (bind(sockfd, res->ai_addr, res->ai_addrlen) == -1) {
    int err_val = errno;
    if (errno != EINTR) {
      syslog(LOG_ERR, "Error creating binding to socket file descriptor: %s",
             strerror(err_val));
      freeaddrinfo(res);
      closelog();
      return -1;
    }
  }

  freeaddrinfo(res);

  int clock_id = CLOCK_MONOTONIC;
  if (timer_create(clock_id, &sev, &timerid) != 0) {
    syslog(LOG_ERR, "Error %d (%s) creating timer!\n", errno, strerror(errno));
  } else {
    struct timespec start_time;
    start_time.tv_sec = 10;
    int timer_period = 10;
    if (setup_timer(clock_id, timerid, timer_period, &start_time)) {
    }
  }
  do {
    if (listen(sockfd, BACKLOG) == -1) {
      err_val = errno;
      if (errno != EINTR) {
        syslog(LOG_ERR,
               "Error when starting to listen on socket file descriptor: %s",
               strerror(err_val));
        closelog();
        return -1;
      }
    }
    new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin);
    if (new_fd == -1) {
      err_val = errno;
      if (errno != EINTR) {
        syslog(LOG_ERR,
               "Error when starting accept on socket file descriptor: %s",
               strerror(err_val));
        closelog();
        return -1;
      }
    }
    new_node = malloc(sizeof(struct node));
    new_data = malloc(sizeof(struct data));
    if (new_node == NULL) {
      syslog(LOG_ERR, "Error memory allocating the new_thread node: %s",
             strerror(err_val));
      closelog();
      return -1;
    }
    inet_ntop(AF_INET, &their_addr.sin_addr, new_data->ip_str, INET_ADDRSTRLEN);
    new_data->new_fd = new_fd;
    new_thread = malloc(sizeof(pthread_t));
    new_data->thread = new_thread;
    new_node->data = new_data;

    pthread_create(new_node->data->thread, NULL, read_write_thread,
                   new_node->data);
    SLIST_INSERT_HEAD(&head, new_node, nodes);

    new_thread = NULL;
    new_data = NULL;
    new_node = NULL;
  } while (!caught_sigint && !caught_sigterm);
  syslog(LOG_DEBUG, "Caught signal, exiting");
  if (remove(FILE_NAME) == -1) {
    int err_val = errno;
    if (errno != EINTR) {
      syslog(LOG_ERR, "Error while removing file: %s", strerror(err_val));
      closelog();
    }
  }
  SLIST_FOREACH(new_node, &head, nodes) {
    pthread_join(*new_node->data->thread, (void **)new_node->data);
  }
  while (!SLIST_EMPTY(&head)) {
    new_node = SLIST_FIRST(&head);
    new_data = new_node->data;
    new_thread = new_data->thread;
    SLIST_REMOVE_HEAD(&head, nodes);
    free(new_thread);
    free(new_data);
    free(new_node);
    new_thread = NULL;
    new_data = NULL;
    new_node = NULL;
  }
  SLIST_INIT(&head);
  if (timer_delete(timerid) != 0) {
    if (errno != EINTR) {
      syslog(LOG_ERR, "Error deleting timer: %s", strerror(err_val));
    }
  }
  shutdown(sockfd, 2);
  return 0;
}

int register_signal_handlers() {
  struct sigaction new_action;
  memset(&new_action, 0, sizeof(struct sigaction));
  new_action.sa_handler = signal_handler;
  int err_val;

  if (sigaction(SIGTERM, &new_action, NULL) != 0) {
    err_val = errno;
    syslog(LOG_ERR, "Error registering handler for SIGTERM: %s",
           strerror(err_val));
    return -1;
  }

  if (sigaction(SIGINT, &new_action, NULL) != 0) {
    err_val = errno;
    syslog(LOG_ERR, "Error registering handler for SIGINT: %s",
           strerror(err_val));
    return -1;
  }

  return 0;
}

bool set_mode(int argc, char *argv[]) {
  if (argc >= 2) {
    if (strcmp(argv[1], "-d") == 0) {
      syslog(LOG_DEBUG, "Starting in daemon mode.");
      return true;
    }
  }

  syslog(LOG_DEBUG, "Starting in user mode.");
  return false;
}

int execute_mode(bool daemon) {
  pid_t childpid;

  if (daemon) {
    switch (childpid = fork()) {
    case -1:
      return -1;
    case 0:
      if (send_and_receive() == -1) {
        return -1;
      }
      break;
    default:
      return 0;
    }
  } else {
    if (send_and_receive() == -1) {
      return -1;
    }
  }

  return 0;
}

int main(int argc, char *argv[]) {
  openlog("aesdsocket", 0, LOG_USER);

  if (register_signal_handlers() == -1) {
    closelog();
    return -1;
  }

  bool daemon = set_mode(argc, argv);

  if (execute_mode(daemon) == -1) {
    closelog();
    return -1;
  }

  closelog();
  return 0;
}
