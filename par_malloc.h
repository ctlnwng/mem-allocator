#ifndef PAR_MALLOC_H
#define PAR_MALLOC_H

typedef struct hm_stats {
    long pages_mapped;
    long pages_unmapped;
    long chunks_allocated;
    long chunks_freed;
    long free_length;
} hm_stats;

typedef struct bucket bucket;

struct bucket {
    size_t size;
    size_t bitmap_size;
    bucket* next;
    char* bitmap;
};

bucket* make_bucket(size_t size);

hm_stats* hgetstats();
void hprintstats();

#endif
