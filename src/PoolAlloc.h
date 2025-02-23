#pragma once

#include <inttypes.h>
#include <malloc.h>
#include <memory.h>
#include <stdbool.h>

#include "snooze.h"

/*
POOL ALLOCATOR:
keeps track of set of allocations to make worrying about freeing them easier.
should be used for not high-perf things, when prototyping, etc.

fns:
poolAllocInit() - returns a good to used pool alloc. Dont copy or modify this. Use deinit to free properly.
poolAllocDeinit() - frees a pool + all allocations within.

poolALlocAlloc() - malloc but in the pool, returned memory is zeroed
poolALlocFree() - free but in the pool
poolAllocPushArray() - take an array allocated in the pool, attempt to add one to it (can grow the arr)
    doubles allocated memory on a grow
*/
// FIXME: adapt this whole file to be in snooze please :) thank u

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
    int nodeCount = 16;
    PoolAlloc out = {
        .nodeCount = nodeCount,
        .nodes = calloc(1, nodeCount * sizeof(*out.nodes)),
    };
    return out;
}

void poolAllocClear(PoolAlloc* pool) {
    for (int i = 0; i < pool->nodeCount; i++) {
        PoolAllocNode* node = &pool->nodes[i];
        if (node->allocated) {
            free(node->allocation);
        }
        memset(node, 0, sizeof(*node));
    }
}

void poolAllocDeinit(PoolAlloc* pool) {
    poolAllocClear(pool);
    free(pool->nodes);
    memset(pool, 0, sizeof(*pool));
}

// new and old size should be in bytes
// asserts on failure
static void* _poolAllocReallocZeroed(void* ptr, uint64_t oldSize, uint64_t newSize) {
    void* outPtr = realloc(ptr, newSize);
    memset((char*)outPtr + oldSize, 0, newSize - oldSize);
    SNZ_ASSERTF(outPtr != NULL, "realloc failed. New size: %lld, old size: %lld", newSize, oldSize);
    return outPtr;
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
    SNZ_ASSERTF(size >= 0, "new allocation with size < 0, was: %lld", size);

    PoolAllocNode* node = NULL;
    for (int i = 0; i < pool->nodeCount; i++) {
        PoolAllocNode* n = &(pool->nodes[i]);
        if (!n->allocated) {
            node = n;
            break;
        }
    }

    if (node == NULL) {
        int64_t newCount = 2 * pool->nodeCount + 1;
        pool->nodes = _poolAllocReallocZeroed(
            pool->nodes,
            pool->nodeCount * sizeof(*pool->nodes),
            newCount * sizeof(*pool->nodes));
        node = &pool->nodes[pool->nodeCount];
        pool->nodeCount = newCount;
    }

    node->allocated = true;
    node->allocation = calloc(1, size + 1);
    SNZ_ASSERT(node->allocation, "pool alloc alloc failed.");
    node->capacity = size;

    // printf("NEW POOL ALLOCATION!!!\n");
    // for (int i = 0; i < pool->nodeCount; i++) {
    //     PoolAllocNode* node = &pool->nodes[i];
    //     printf("\tNode: allocated: %d, alloc: %p, size: %lld\n", node->allocated, node->allocation, node->capacity);
    // }

    return node->allocation;
}

// FIXME: macro
void* poolAllocGrow(PoolAlloc* pool, void* alloc, int64_t newSize) {
    PoolAllocNode* node = _poolAllocFindAlloc(pool, alloc);
    SNZ_ASSERTF(node != NULL, "Allocation to grow could not be found, ptr: %p", alloc);
    SNZ_ASSERTF(node->allocated, "Trying to grow non allocated node. ptr: %p", alloc);

    SNZ_ASSERTF(newSize > node->capacity, "Grow fails, new size (%d) <= old (%d).", newSize, node->capacity);
    node->allocation = realloc(node->allocation, newSize);
    node->capacity = newSize;
    SNZ_ASSERTF(node->allocation, "Realloc returned a null ptr. requested size: %d", newSize);
    return node->allocation;
}

void poolAllocFree(PoolAlloc* pool, void* alloc) {
    PoolAllocNode* node = _poolAllocFindAlloc(pool, alloc);
    SNZ_ASSERTF(node != NULL, "allocation to free was not found. ptr: %p", alloc);
    SNZ_ASSERTF(node->allocated, "allocation to free was already free. ptr: %p", alloc);
    free(node->allocation);
    memset(node, 0, sizeof(*node));
}

// count and arrayPtr should be int64_t and T* respectively, they are writen to as output, returns the addr of the new elt
// pushes 1 elt to the array, count should be the counter for the length of the arr
// array may be null, in which case a new one will be allocated
#define poolAllocPushArray(poolPtr, arrayPtr, count, T) (_poolAllocPushArray((poolPtr), (void**)(&(arrayPtr)), &(count), sizeof(T)), &arrayPtr[count - 1])
void _poolAllocPushArray(PoolAlloc* pool, void** array, int64_t* count, int64_t sizeOfElt) {
    if (*array != NULL) {
        PoolAllocNode* node = _poolAllocFindAlloc(pool, *array);
        SNZ_ASSERTF(node != NULL, "allocation to grow was not found. ptr: %p", *array);

        int64_t newSize = (*count * 2 + 1) * sizeOfElt;
        node->capacity = newSize;
        (*count)++;

        node->allocation = realloc(node->allocation, newSize);
        SNZ_ASSERTF(node->allocation != NULL, "Realloc returned NULL. ptr: %d", *array);
        *array = node->allocation;  // write to the output :) // FIXME: this shit very dangerous, typecheck at least
    } else {
        SNZ_ASSERT(*count == 0, "uninitialized arr with non-zero count.");
        *array = poolAllocAlloc(pool, sizeOfElt);
        *count = 1;
    }
}

void _poolAllocTests() {
    PoolAlloc p = poolAllocInit();
    PoolAlloc* pool = &p;

    char* myArr = poolAllocAlloc(pool, 0);
    int64_t myArrCount = 0;
    for (int i = 0; i < 10; i++) {
        *poolAllocPushArray(pool, myArr, myArrCount, char) = '0' + i;
    }
    void* alloc = poolAllocAlloc(pool, 64);
    poolAllocAlloc(pool, 64);
    poolAllocFree(pool, alloc);
    poolAllocAlloc(pool, 64);
    poolAllocDeinit(pool);
}
