

#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>

#include "par_malloc.h"
#include "xmalloc.h"

static size_t PAGE_SIZE = 4096;
static size_t HEADER_SIZE = sizeof(long) + (sizeof(size_t) * 2) + sizeof(bucket*) + sizeof(bitmap_t);
static size_t NUM_ARENAS = 10;

// possibly remove 4 and 8 because chunk header is 8 bytes
size_t bucket_sizes[] = {12, 16, 24, 32, 48, 64, 96, 128, 192, 256,
        384, 512, 768, 1024, 1536, 2048, 3192, 4096};
size_t num_bucket_sizes = 17; // 18

static bucket*** arenas;
pthread_mutex_t* arena_mutexes;

static
size_t
div_up(size_t xx, size_t yy)
{
    // This is useful to calculate # of pages
    // for large allocations.
    size_t zz = xx / yy;

    if (zz * yy == xx) {
        return zz;
    }
    else {
        return zz + 1;
    }
}

size_t
convert_to_bytes(size_t bytes)
{
    return (size_t)ceil((double)bytes / 8);
}


// NOTE: The following bitmap functions were taken from the following
// stack overflow thread: https://stackoverflow.com/questions/16947492/looking-for-a-bitmap-implementation-api-in-linux-c
void set_bit(bitmap_t b, int i)
{
    b[i / 8] |= 1 << (i & 7);
}

void unset_bit(bitmap_t b, int i)
{
    b[i / 8] &= ~(1 << (i & 7));
}

int get_bit(bitmap_t b, int i)
{
    return b[i / 8] & (1 << (i & 7)) ? 1 : 0;
}

int
find_alloc_bit_idx(bitmap_t b, int bitmap_size)
{
    int idx;

    for (int i = 0; i < bitmap_size; i++) {
        if (get_bit(b, i) == 0) {
            set_bit(b, i);
            return i;
        }
    }

    return -1;
}

bucket*
make_bucket(int arena_id, size_t size)
{
    double temp = (PAGE_SIZE - HEADER_SIZE);
    size_t num_chunks = floor((temp * 8) / (((double)size * 8) + 1));
    
    // size_t bucket_size = (size > 768) ? 4 * PAGE_SIZE : PAGE_SIZE;
    size_t bucket_size = PAGE_SIZE;

    bucket* new_bucket = (bucket*) mmap(NULL, bucket_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    new_bucket->arena_id = arena_id;
    new_bucket->size = size;
    new_bucket->bitmap_size = num_chunks;
    new_bucket->next = NULL;
    new_bucket->bitmap = (void*)new_bucket + HEADER_SIZE;
    new_bucket->bitmap = memset(new_bucket->bitmap, 0, convert_to_bytes(num_chunks));

    return new_bucket;    
}

void
initialize_buckets(int arena_id)
{
    arenas[arena_id] = (bucket**) mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    bucket** buckets = arenas[arena_id];

    for (int i = 0; i < num_bucket_sizes; i++) {
        buckets[i] = make_bucket(arena_id, bucket_sizes[i]);
    } 
}

void
initialize_arenas()
{ 
    size_t num_pages_arena = div_up((NUM_ARENAS * sizeof(bucket**)), PAGE_SIZE);
    size_t num_pages_mutex = div_up((NUM_ARENAS * sizeof(pthread_mutex_t)), PAGE_SIZE);
    arenas = (bucket***)mmap(NULL, num_pages_arena * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    arena_mutexes = (pthread_mutex_t*)mmap(NULL, num_pages_mutex * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    for (int i = 0; i < NUM_ARENAS; i++) {
        initialize_buckets(i);
        pthread_mutex_init(&arena_mutexes[i], NULL);
    } 
}

bucket*
find_cur_bucket(int arena_id, size_t size) 
{
    bucket** buckets = arenas[arena_id];
    bucket* cur_bucket;
    for (int i = 0; i < num_bucket_sizes; i++) {
        if (bucket_sizes[i] >= size) {
            cur_bucket = buckets[i];
            break;
        }
    }

    return cur_bucket;
}


void*
xmalloc(size_t bytes)
{
    pthread_t thread_id = pthread_self();
    
    bytes = bytes + sizeof(size_t);

    if (bytes > 3192) {
        size_t num_pages = div_up(bytes, PAGE_SIZE);
        return mmap(NULL, num_pages * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    }

    if (!arenas) {
        initialize_arenas();
    }

    int arena_idx = thread_id % NUM_ARENAS;
    bucket** cur_arena = arenas[arena_idx];

    if (pthread_mutex_trylock(&arena_mutexes[arena_idx]) == 1) {
      arena_idx = (arena_idx + 1) % NUM_ARENAS;
      cur_arena = arenas[arena_idx];
      pthread_mutex_lock(&arena_mutexes[arena_idx]);
    }

    bucket* cur_bucket = find_cur_bucket(arena_idx, bytes);

    int alloc_bit_idx = find_alloc_bit_idx(cur_bucket->bitmap, cur_bucket->bitmap_size);
    
    while (alloc_bit_idx == -1) {
        if (cur_bucket->next == NULL) {
            cur_bucket->next = make_bucket(arena_idx, cur_bucket->size);
            alloc_bit_idx = 0;
            cur_bucket = cur_bucket->next;
            break;
        }

        cur_bucket = cur_bucket->next;
        alloc_bit_idx = find_alloc_bit_idx(cur_bucket->bitmap, cur_bucket->bitmap_size);
    }

    void* chunk = ((void*)cur_bucket) + HEADER_SIZE + convert_to_bytes(cur_bucket->bitmap_size) + (alloc_bit_idx * cur_bucket->size);

    pthread_mutex_unlock(&arena_mutexes[arena_idx]);   
    
    *(size_t *)chunk = bytes;
    chunk = chunk + sizeof(size_t);
    return chunk;
}

void
xfree(void* ptr)
{
    ptr = ptr - sizeof(size_t);

    
    size_t size = *(size_t *)ptr;
    
    if (size > 3192) {
        size_t num_pages = div_up(size, PAGE_SIZE);
        munmap(ptr, num_pages * PAGE_SIZE);
        return;
    }

    bucket* cur_bucket = (bucket*)(void*)((uintptr_t)ptr & (uintptr_t)0xFFFFFFFFF000);
    // bucket* cur_bucket = find_cur_bucket(size);
    

    // size_t bucket_size = (size > 768) ? 4 * PAGE_SIZE : PAGE_SIZE;

    /*
    while ((void*)cur_bucket + PAGE_SIZE < ptr) {
        cur_bucket = cur_bucket->next;
    }
    */
    
    int bit_idx_to_free = (ptr - ((void *)cur_bucket + HEADER_SIZE + convert_to_bytes(cur_bucket->bitmap_size))) / cur_bucket->size;
    
    unset_bit(cur_bucket->bitmap, bit_idx_to_free);
}

void*
xrealloc(void* prev, size_t bytes)
{
    void* new_ptr = xmalloc(bytes);
    size_t old_size = *(size_t*)(prev - sizeof(size_t));

    memcpy(new_ptr, prev, old_size);
    xfree(prev);

    return new_ptr;
}

