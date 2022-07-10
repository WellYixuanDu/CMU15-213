#include "csapp.h"
#include <sys/time.h>
#include <pthread.h>

#define TYPES 6
extern const int cache_block_size[];
extern const int cache_cnt[];

typedef struct cache_block {
    char *url;
    char *data;
    int datasize;
    int time;
    pthread_rwlock_t rwlock;
} cache_block;

typedef struct cache_type {
    cache_block *cacheobjs;
    int size;
 } cache_type;

cache_type caches[TYPES];

void init_cache();

int read_cache(char *url, int fd);

void write_cache(char *url, char *data, int len);

void free_cache();