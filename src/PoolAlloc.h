#include <inttypes.h>
#include <malloc.h>
#include <memory.h>
#include <stdbool.h>

/*
POOL ALLOCATOR:
keeps track of set of allocations to make worrying about freeing them easier.
should be used for not high-perf things, when prototyping, etc.

fns:
poolAllocInit() - returns a good to used pool alloc. Dont copy or modify this. Use deinit to free properly.
poolAllocDeinit() - frees a pool + all allocations within.

poolALlocAlloc() - malloc but in the pool

*/

typedef struct PoolAllocNode PoolAllocNode;
struct PoolAllocNode {
    void* allocation;
    int64_t capacity;
    bool allocated;
};

typedef struct {
    PoolAllocNode* nodes;
    int64_t nodeCount;
} PoolAlloc;

// FIXME: this whole thing could be O(1) instead of o(n) if lookup metadata was stored at the allocation :)

PoolAlloc poolAllocInit() {
    PoolAlloc out;
    memset(&out, sizeof(out), 0);
    out.nodeCount = 16;
    out.nodes = calloc(1, out.nodeCount * sizeof(*out.nodes));
    return out;
}

void poolAllocDeinit(PoolAlloc* pool) {
    for (int i = 0; i < pool->nodeCount; i++) {
        PoolAllocNode* node = &pool->nodes[i];
        if (node->allocated) {
            free(node->allocation);
        }
    }
    free(pool->nodes);
    memset(pool, 0, sizeof(*pool));
}

static PoolAllocNode* _poolAllocFindAlloc(PoolAlloc* pool, void* alloc) {
    for (int i = 0; i < pool->nodeCount; i++) {
        PoolAllocNode* n = &pool->nodes[i];
        if (n->allocation == alloc) {
            return n;
        }
    }
    return NULL;
}

// FIXME: macro
void* poolAllocAlloc(PoolAlloc* pool, int64_t size) {
    PoolAllocNode* node = NULL;
    for (int i = 0; i < pool->nodeCount; i++) {
        PoolAllocNode* n = &pool->nodes[i];
        if (!node->allocated) {
            node = n;
            break;
        }
    }

    if (node == NULL) {
        int64_t newCount = 2 * pool->nodeCount + 1;
        pool->nodes = realloc(pool->nodes, newCount * sizeof(*pool->nodes));
        if (pool->nodes == NULL) {
            return NULL;
        }
        node = &pool->nodes[pool->nodeCount];
        pool->nodeCount = newCount;
    }

    node->allocated = true;
    what happens when size is zero ? ;
    node->allocation = malloc(size);
    node->capacity = size;
    return node->allocation;
}

// FIXME: macro
void* poolAllocGrow(PoolAlloc* pool, void* alloc, int64_t newSize) {
    PoolAllocNode* node = _poolAllocFindAlloc(pool, alloc);
    assert(node != NULL);
    assert(node->allocated);

    node->allocation = realloc(node->allocation, newSize);
    assert(newSize > node->capacity);
    node->capacity = newSize;
    assert(node->allocation != NULL);
    return node->allocation;
}

void poolAllocFree(PoolAlloc* pool, void* alloc) {
    PoolAllocNode* node = _poolAllocFindAlloc(pool, alloc);
    assert(node != NULL);
    assert(node->allocated);

    free(node->allocation);
    node->allocated = false;
}

#define poolAllocPushArray(poolPtr, array, count, T) _poolAllocPushArray((poolPtr), (array), &(count), sizeof(T))
void* _poolAllocPushArray(PoolAlloc* pool, void* array, int64_t* count, int64_t sizeOfElt) {
    PoolAllocNode* node = _poolAllocFindAlloc(pool, array);
    assert(node != NULL);
    assert(node->allocated);

    int64_t newSize = *count * 2 * sizeOfElt;
    node->capacity = newSize;
    (*count)++;

    node->allocation = realloc(node->allocation, newSize);
    assert(node->allocation != NULL);
    return node->allocation;
}

void poolAllocTests() {
    PoolAlloc p = poolAllocInit();
    PoolAlloc* pool = &p;

    char* myArr = poolAllocAlloc(pool, 0);
    int64_t myArrCount = 0;
    myArr = poolAllocPushArray(pool, myArr, myArrCount, char);
}