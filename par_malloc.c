

#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#include "par_malloc.h"
#include "xmalloc.h"

static size_t PAGE_SIZE = 4096;
static bucket** buckets;

// possibly remove 4 and 8 because chunk header is 8 bytes
size_t bucket_sizes[] = {4, 8, 12, 16, 24, 32, 48, 64, 96, 128, 192, 256,
        384, 512, 768, 1024, 1536, 2048, 3192, 4096};
size_t num_bucket_sizes = 20;

bucket*
make_bucket(size_t size)
{
    double temp = (PAGE_SIZE - ((sizeof(size_t) * 2) + sizeof(bucket*)));
    double num_chunks = floor((temp * 8) / (((double)size * 8) + 1));
    size_t bitmap_size = (size_t) ceil(num_chunks / 8);

    bucket* new_bucket = (bucket*) mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    new_bucket->size = size;
    new_bucket->bitmap_size = bitmap_size;
    new_bucket->next = NULL;    
}

void
initialize_buckets()
{
    buckets = (bucket**) mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    for (int i = 0; i < num_bucket_sizes; i++) {
        buckets[i] = make_bucket(bucket_sizes[i]);
    }
    
}

bucket*
find_cur_bucket(size_t size) 
{
    bucket* cur_bucket;
    for (int i = 0; i < num_bucket_sizes; i++) {
        if (bucket_sizes[i] >= size) {
            cur_bucket = buckets[i];
            break;
        }
    }
}

void*
xmalloc(size_t bytes)
{
    // TODO: deal with big allocations
    bytes = bytes + sizeof(size_t);

    //return opt_malloc(bytes);
    if (!buckets) {
        initialize_buckets();
    }

    bucket* cur_bucket = find_cur_bucket(bytes);

    // TODO: traverse cur_bucket->bitmap to find free chunk
    int alloc_bit_idx = find_alloc_bit_idx(cur_bucket->bitmap);
    
    while (alloc_bit_idx == -1) {
        if (cur_bucket->next == NULL) {
            cur_bucket->next = make_bucket(cur_bucket->size);
            alloc_bit_idx = 0;
            cur_bucket = cur_bucket->next;
            break;
        }

        cur_bucket = cur_bucket->next;
        alloc_bit_idx = find_alloc_bit_idx(cur_bucket->bitmap);
    }
    
    void* chunk = ((void*)cur_bucket) + 24 + cur_bucket->bitmap_size + (alloc_bit_idx * cur_bucket->size);
    *(size_t *)chunk = bytes;
    chunk = chunk + sizeof(size_t)
    return chunk;
}

void
xfree(void* ptr)
{
    ptr = ptr - sizeof(size_t);
    size_t size = *(size_t *)ptr;
    bucket* cur_bucket = find_cur_bucket(size);

    while ((void*)cur_bucket + PAGE_SIZE < ptr) {
        cur_bucket = cur_bucket->next;
    }
    
    int bit_idx_to_free = (ptr - ((void *)cur_bucket + 24 + cur_bucket->bitmap_size)) / cur_bucket->size;
    //TODO: flip bit_idx_to_free bit on cur_bucket's bitmap to free chunk
    free_bit(cur_bucket->bitmap, bit_idx_to_free);
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

