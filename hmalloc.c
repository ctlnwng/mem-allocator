
#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>

#include "hmalloc.h"

/*
  typedef struct hm_stats {
  long pages_mapped;
  long pages_unmapped;
  long chunks_allocated;
  long chunks_freed;
  long free_length;
  } hm_stats;
*/

/*
  typedef struct free_list {
  size_t size;
  free_list* next;
  } free_list;
*/

const size_t PAGE_SIZE = 4096;
static hm_stats stats; // This initializes the stats to 0.
static free_list* flist;

long
free_list_length()
{
    long num_chunks = 0;
    free_list* flist_ptr = flist;
    while(flist_ptr) {
        num_chunks++;
        flist_ptr = flist_ptr->next;
    }
    return num_chunks;
}

hm_stats*
hgetstats()
{
    stats.free_length = free_list_length();
    return &stats;
}

void
hprintstats()
{
    stats.free_length = free_list_length();
    fprintf(stderr, "\n== husky malloc stats ==\n");
    fprintf(stderr, "Mapped:   %ld\n", stats.pages_mapped);
    fprintf(stderr, "Unmapped: %ld\n", stats.pages_unmapped);
    fprintf(stderr, "Allocs:   %ld\n", stats.chunks_allocated);
    fprintf(stderr, "Frees:    %ld\n", stats.chunks_freed);
    fprintf(stderr, "Freelen:  %ld\n", stats.free_length);
}

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

void
remove_chunk(free_list* chunk) 
{
    free_list* flist_ptr = flist;
    free_list* flist_prev = 0;
    while (flist_ptr) {
        if (chunk == flist_ptr) {
            if (!flist_prev) {
                flist = chunk->next;
                break;
            }
            else {
                flist_prev->next = chunk->next;
                break;
            }
        }
        flist_prev = flist_ptr;
        flist_ptr = flist_ptr->next;
    }
}

// adds the chunk in ascending address order
void
add_chunk(free_list* chunk)
{
    chunk->next = NULL;

    if (!flist) {
        flist = chunk;
        return;
    }

    free_list* flist_ptr = flist;
    free_list* flist_prev = 0;
    while (flist_ptr) {
        if (flist_ptr->next == NULL && chunk > flist_ptr) {
            flist_ptr->next = chunk;
            break;
        }
        if (chunk < flist_ptr && chunk > flist_prev) {
            chunk->next = flist_ptr;
            if (flist_prev) {
                flist_prev->next = chunk;
            }
            else {
                flist = chunk;
            } 
            break;
        }
        flist_prev = flist_ptr;
        flist_ptr = flist_ptr->next;
    }
}

void 
coalesce()
{
    free_list* flist_ptr = flist;
    while (flist_ptr && flist_ptr->next) {
        if ((void *)flist_ptr + flist_ptr->size == flist_ptr->next) {
            flist_ptr->size = flist_ptr->size + flist_ptr->next->size;
            flist_ptr->next = flist_ptr->next->next;
        }
        else {
            flist_ptr = flist_ptr->next;
        }
    }
}

void*
hmalloc(size_t size)
{
    void* ptr = NULL;
    stats.chunks_allocated += 1;
    size += sizeof(size_t);

    if (size >= PAGE_SIZE) {
        size_t num_pages = div_up(size, PAGE_SIZE);
        ptr = mmap(NULL, num_pages * PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
        stats.pages_mapped += num_pages;
    }
    else {
        // search the free list
        free_list* flist_ptr = flist;
        while (flist_ptr) {
            // if we find a matching chunk, remove it from the free list
            if (flist_ptr->size >= size) {
                remove_chunk(flist_ptr);
                ptr = flist_ptr;
                size_t unused_size = flist_ptr->size - size;
                // if matching chunk isn't equal to allocation size and large enough for a free list node,
                // put the remainder on the free list
                if (unused_size >= sizeof(free_list)) {     
                    free_list* remaining_chunk = (free_list *)((void *)flist_ptr + size);
                    remaining_chunk->size = unused_size;
                    add_chunk(remaining_chunk);
                }
                break;
            }
            flist_ptr = flist_ptr->next;
        }
        // if there's no matching chunk, map a new page
        if (ptr == 0) {
            ptr = mmap(NULL, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
            stats.pages_mapped += 1;
            if (PAGE_SIZE - size >= sizeof(free_list)) {
                free_list* remaining_chunk = (free_list *)((void*)ptr + size);
                remaining_chunk->size = PAGE_SIZE - size;
                add_chunk(remaining_chunk);
            }
        }
    }
    *(size_t *)ptr = size;
    ptr = ptr + sizeof(size_t);
    return (void*) ptr;
}

void
hfree(void* item)
{
    stats.chunks_freed += 1;
    item = item - sizeof(size_t);
    size_t size = *(size_t *)item;
    // if the chunk is larger than a page unmap it
    if (size >= PAGE_SIZE) {
        size_t num_pages = div_up(size, PAGE_SIZE);
        munmap(item, num_pages * PAGE_SIZE);
        stats.pages_unmapped += num_pages;
    }
    // else add chunk to free list and coalesce
    else {
        free_list* chunk = (free_list*)item;
        chunk->size = size;
        add_chunk(chunk);
        coalesce();
    }
}

void*
hrealloc(void* prev, size_t size)
{
    void* new_ptr = hmalloc(size);

    size_t old_size = *(size_t*)(prev - sizeof(size_t));
    memcpy(new_ptr, prev, old_size);
    
    hfree(prev);
    return new_ptr;
}
