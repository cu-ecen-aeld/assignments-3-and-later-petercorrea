#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#define DEBUG_LOG(msg, ...)
#define ERROR_LOG(msg, ...) printf("threading ERROR: " msg "\n", ##__VA_ARGS__)

void *threadfunc(void *thread_param)
{
    // destructure the data
    struct thread_data *thread_args = (struct thread_data *)thread_param;
    int wait = thread_args->wait_to_obtain_ms;
    int hold = thread_args->wait_to_release_ms;
    pthread_mutex_t *mutx = thread_args->mutex;

    // perform ops
    usleep(wait);
    pthread_mutex_lock(mutx);
    usleep(hold);
    pthread_mutex_unlock(mutx);
    thread_args->thread_complete_success = true;
    return thread_param;
}

bool start_thread_obtaining_mutex(
    pthread_t *thread,
    pthread_mutex_t *mutex,
    int wait_to_obtain_ms,
    int wait_to_release_ms)
{
    // alloc
    struct thread_data *thread_param = (struct thread_data *)malloc(sizeof(struct thread_data));

    // err
    if (thread_param == NULL)
    {
        return false;
    }

    // set data
    thread_param->wait_to_obtain_ms = wait_to_obtain_ms;
    thread_param->wait_to_release_ms = wait_to_release_ms;
    thread_param->mutex = mutex;

    // exec
    if (pthread_create(thread, NULL, threadfunc, (void *)thread_param) == 0)
    {
        return true;
    }
    return false;
}
