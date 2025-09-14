// mempool.h
#ifndef MEMPOOL_H
#define MEMPOOL_H
#include <stddef.h>
typedef struct mempool mempool_t;
mempool_t* mempool_create(size_t item_size, size_t count);
void mempool_destroy(mempool_t* m);
void* mempool_alloc(mempool_t* m);
void mempool_free(mempool_t* m, void* p);
#endif
