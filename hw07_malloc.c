
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "xmalloc.h"
#include "hmalloc.h"

/* CH02 TODO:
 *  - This should call / use your alloctor from the previous HW,
 *    modified to be thread-safe and have a realloc function.
 */

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void*
xmalloc(size_t bytes)
{
    pthread_mutex_lock(&mutex);
    void* ptr = hmalloc(bytes);
    pthread_mutex_unlock(&mutex);

    return ptr;
}

void
xfree(void* ptr)
{
    pthread_mutex_lock(&mutex);
    hfree(ptr);
    pthread_mutex_unlock(&mutex);
}

void*
xrealloc(void* prev, size_t bytes)
{
    pthread_mutex_lock(&mutex);
    void* ptr = hrealloc(prev, bytes);
    pthread_mutex_unlock(&mutex);

    return ptr;
}

