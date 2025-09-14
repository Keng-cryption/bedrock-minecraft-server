// mempool.c
#include "mempool.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

struct mempool {
    size_t item_size;
    size_t count;
    void*  block;
    void** free_list;
    size_t free_count;
};

mempool_t* mempool_create(size_t item_size, size_t count) {
    mempool_t* m = malloc(sizeof(*m));
    if(!m) return NULL;
    m->item_size = (item_size + sizeof(void*) - 1) & ~(sizeof(void*) - 1);
    m->count = count;
    m->block = aligned_alloc(64, m->item_size * count);
    if(!m->block) { free(m); return NULL; }
    m->free_list = malloc(sizeof(void*) * count);
    for(size_t i=0;i<count;i++){
        m->free_list[i] = (char*)m->block + i * m->item_size;
    }
    m->free_count = count;
    return m;
}

void mempool_destroy(mempool_t* m){
    if(!m) return;
    free(m->block);
    free(m->free_list);
    free(m);
}

void* mempool_alloc(mempool_t* m){
    if(m->free_count == 0) return NULL;
    void* p = m->free_list[--m->free_count];
    return p;
}

void mempool_free(mempool_t* m, void* p){
    if(m->free_count >= m->count) return; // double free guard
    m->free_list[m->free_count++] = p;
}
