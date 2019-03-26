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

typedef unsigned long* bitmap_t;

struct bucket {
    long arena_id;
    size_t size;
    size_t bitmap_size; // in bits
    bucket* next;
    bitmap_t bitmap;
};

bucket* make_bucket(int arena_id, size_t size);

hm_stats* hgetstats();
void hprintstats();

void set_bit(bitmap_t b, int i);
void unset_bit(bitmap_t b, int i);
int get_bit(bitmap_t b, int i);

#endif
