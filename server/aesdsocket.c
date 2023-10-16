#include <sys/socket.h>
#include <sys/wait.h>
#include <netdb.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/queue.h>
#include <time.h>

#define PORT "9000"
#define BACKLOG 10
#define USE_AESD_CHAR_DEVICE 1

#if USE_AESD_CHAR_DEVICE
     /* This one if debugging is on, and kernel space */
#    define FILE_NAME "/dev/aesdchar"
#else
     /* This one for user space */
#    define FILE_NAME "/var/tmp/aesdsocketdata"
#endif

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
    if (signal_number == SIGINT) {
        caught_sigint = true;
    } 
    else if (signal_number == SIGTERM) {
        caught_sigterm = true;
    }
}

#if (USE_AESD_CHAR_DEVICE == 0)
static void timer_thread (union sigval sigval) {
    char time_string[1024];
    int rc;
    time_t t;
    struct tm *tmp;
    t = time(NULL);
    tmp = localtime(&t);
    rc = strftime(time_string, sizeof(time_string), "timestamp:%a, %d %b %Y %T %z\n", tmp);
    if (rc == 0) {
        if (errno != EINTR) {
            syslog(LOG_ERR, "Error setting time_string with strftime from timer_thread: %s", strerror(errno));
        }
    }
    if ( pthread_mutex_lock(&read_write_mutex) != 0 ) {
        if (errno != EINTR) {
            syslog(LOG_ERR, "Error %d (%s) locking thread data!",errno,strerror(errno));
        }
    } else {
        FILE *file_to_write = fopen(FILE_NAME, "a+");
        fwrite(time_string, sizeof time_string[0], strlen(time_string), file_to_write);
        fclose(file_to_write);
        if ( pthread_mutex_unlock(&read_write_mutex) != 0 ) {
            if (errno != EINTR) {
                syslog(LOG_ERR, "Error %d (%s) unlocking thread data!\n",errno,strerror(errno));
            }
        }
    }
}
#endif
#if (USE_AESD_CHAR_DEVICE == 0)
static bool setup_timer( int clock_id,
                         timer_t timerid, unsigned int timer_period,
                         struct timespec *start_time)
{
    bool success = false;
    if ( timer_period == 0 ) {
        syslog(LOG_ERR, "Unsupported timer period (must be non-zero)");
    } else {
        if ( clock_gettime(clock_id,start_time) != 0 ) {
            syslog(LOG_ERR, "Error %d (%s) getting clock %d time\n",errno,strerror(errno),clock_id);
        } else {
            struct itimerspec itimerspec;
            memset(&itimerspec, 0, sizeof(struct itimerspec));
            itimerspec.it_interval.tv_sec = timer_period;
            itimerspec.it_interval.tv_nsec = 0;
            itimerspec.it_value.tv_sec = start_time->tv_sec + 
                                         itimerspec.it_interval.tv_sec;
            if( timer_settime(timerid, TIMER_ABSTIME, &itimerspec, NULL ) != 0 ) {
                syslog(LOG_ERR, "Error %d (%s) setting timer\n",errno,strerror(errno));
            } else {
                success = true;
            }
        }
    }
    return success;
}
#endif
void* read_write_thread(void* thread_param) {
    int buf_size = 1024;
    char *buf = malloc(buf_size * sizeof(char));
    int rc, byte_count, err_val;

    struct data* thread_args = (struct data*) thread_param;

    syslog(LOG_DEBUG, "Accepted connection to %s\n", thread_args->ip_str);
    rc = pthread_mutex_lock(&read_write_mutex);
    FILE *file_to_write = fopen(FILE_NAME, "a+");
    if (!file_to_write) {
        err_val = errno;
        if (errno != EINTR) {
            syslog(LOG_ERR, "Error opening /var/tmp/aesdsocketdata: %s", strerror(err_val));
            free(buf);
            pthread_exit(thread_param);
        }
    }
    if (rc != 0) {
        err_val = errno;
        if (errno != EINTR) {
            syslog(LOG_ERR, "Error locking mutex for read_write_thread: %s", strerror(err_val));
            free(buf);
            pthread_exit(thread_param);
        }
    }
    byte_count = recv(thread_args->new_fd, buf, buf_size, 0);
    while(byte_count == buf_size) { 
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
    rc = pthread_mutex_unlock(&read_write_mutex);
    if (rc != 0) {
        err_val = errno;
        if (errno != EINTR) {
            syslog(LOG_ERR, "Error unlocking mutex for read_write_thread: %s", strerror(err_val));
            free(buf);
            pthread_exit(thread_param);
        } 
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

    struct node * new_node = NULL;
    struct data * new_data = NULL;
    pthread_t * new_thread = NULL;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

#if (USE_AESD_CHAR_DEVICE == 0)
    timer_t timerid;
    struct sigevent sev;
    memset(&sev,0,sizeof(struct sigevent));
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = timer_thread;
#endif 
    int yes=1;

    getaddrinfo(NULL, PORT, &hints, &res);
    syslog(LOG_DEBUG, "Attempting to create a socket file descriptor");
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sockfd == -1) {
        int err_val = errno;
        if (errno != EINTR) {
            syslog(LOG_ERR, "Error creating a socket file descriptor: %s", strerror(err_val));
            freeaddrinfo(res);
            closelog();
            return -1;
        }
    }
    if (setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof yes) == -1) { 
        int err_val = errno;
        if (errno != EINTR) {
            syslog(LOG_ERR, "Error adjusting socket file descriptor options: %s", strerror(err_val));
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
            syslog(LOG_ERR, "Error creating binding to socket file descriptor: %s", strerror(err_val));
            freeaddrinfo(res);
            closelog();
            return -1;
        }
    }
    
    freeaddrinfo(res);
#if (USE_AESD_CHAR_DEVICE == 0)
    int clock_id = CLOCK_MONOTONIC;
    if ( timer_create(clock_id,&sev,&timerid) != 0 ) {
        syslog(LOG_ERR, "Error %d (%s) creating timer!\n",errno,strerror(errno));
    } else {
        struct timespec start_time;
        start_time.tv_sec = 10;
        int timer_period = 10;
        if ( setup_timer(clock_id, timerid, timer_period, &start_time) ) {
        }
    }
#endif
    do {
        if (listen(sockfd, BACKLOG) == -1) {
            err_val = errno;
            if (errno != EINTR) {
                syslog(LOG_ERR, "Error when starting to listen on socket file descriptor: %s", strerror(err_val));
                closelog();
                return -1;
            }
        }
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin);
        if (new_fd == -1) { 
            err_val = errno;
            if (errno != EINTR) { 
                syslog(LOG_ERR, "Error when starting accept on socket file descriptor: %s", strerror(err_val));
                closelog();
                return -1;
            }
        }
        new_node = malloc(sizeof(struct node));
        new_data = malloc(sizeof(struct data));
        if (new_node == NULL) {
                syslog(LOG_ERR, "Error memory allocating the new_thread node: %s", strerror(err_val));
                closelog();
                return -1;
        }
        inet_ntop(AF_INET, &their_addr.sin_addr, new_data->ip_str, INET_ADDRSTRLEN);
        new_data->new_fd = new_fd;
        new_thread = malloc(sizeof(pthread_t));
        new_data->thread = new_thread;
        new_node->data = new_data;

        pthread_create(new_node->data->thread, NULL, read_write_thread, new_node->data);
        SLIST_INSERT_HEAD(&head, new_node, nodes);

        new_thread = NULL;
        new_data = NULL;
        new_node = NULL;
    }
    while(!caught_sigint && !caught_sigterm);
    syslog(LOG_DEBUG, "Caught signal, exiting");
    if (remove(FILE_NAME) == -1) {
        int err_val = errno;
        if (errno != EINTR) {
            syslog(LOG_ERR, "Error while removing file: %s", strerror(err_val));
            closelog();
        }
    }
    SLIST_FOREACH(new_node, &head, nodes) {
        pthread_join(*new_node->data->thread, (void**)new_node->data);
    }
    while(!SLIST_EMPTY(&head)) {
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
#if (USE_AESD_CHAR_DEVICE == 0)
    if(timer_delete(timerid) != 0) {
        if (errno != EINTR) {
            syslog(LOG_ERR, "Error deleting timer: %s", strerror(err_val));
        }
    }
#endif
    shutdown(sockfd, 2);
    return 0;
}

int main(int argc, char* argv[]) {
    pid_t childpid;
    bool daemon = false;

    struct sigaction new_action;

    openlog("aesdsocket", 0, LOG_USER);
    memset(&new_action, 0, sizeof(struct sigaction));
    new_action.sa_handler = signal_handler;
    if (sigaction(SIGTERM, &new_action, NULL) != 0) {
        int err_val = errno;
        syslog(LOG_ERR, "Error registering handler for SIGTERM: %s", strerror(err_val));
        closelog();
        return -1;
    }
    if (sigaction(SIGINT, &new_action, NULL) != 0) {
        int err_val = errno;
        syslog(LOG_ERR, "Error registering handler for SIGINT: %s", strerror(err_val));
        closelog();
        return -1;
    }
    
    if (argc >= 2) {
        if (strcmp(argv[1],"-d") == 0) {
            daemon = true;
            syslog(LOG_DEBUG, "Starting in daemon mode.");
        }
    } else {
        syslog(LOG_DEBUG, "Starting in user mode.");
    }

    if(daemon) {
        switch(childpid = fork()) {
            case -1:
                closelog();
                return -1;
            case 0:
                if (send_and_receive() == -1) {
                    closelog();
                    return -1;
                }
            default:
                closelog();
                return 0;
        }
    }
    else {
        if (send_and_receive() == -1) {
            closelog();
            return -1;
        }
    }
    closelog();
    return 0;
}