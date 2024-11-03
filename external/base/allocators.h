#pragma once
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>

// uses malloc and free on allocate and free
// fixed size
// zeroes memory on allocation, not on release (and not on construction)
struct BumpAlloc {
    void* start;
    void* end;
    int64_t reserved;
    const char* name;
};
typedef struct BumpAlloc BumpAlloc;

BumpAlloc bump_init(int64_t size, void* memory, const char* name);

BumpAlloc bump_allocate(int64_t size, const char* name);
void bump_free(BumpAlloc* a);

// asserts on failure, does not grow
void* bump_push(BumpAlloc* a, int64_t size);

// asserts on failure
void bump_pop(BumpAlloc* a, int64_t size);

void bump_clear(BumpAlloc* a);

// push new pushes sizeof(type) and zero initializes memory
// [bump] is expected to be a pointer
#define BUMP_PUSH_NEW(bump, type) ((type*)(bump_push(bump, sizeof(type))))

// push arr pushes space for count objects of type, memory is zeroed
// [bump] is expected to be a pointer
#define BUMP_PUSH_ARR(bump, count, type) (type*)(bump_push(bump, sizeof(type) * (count)))

char* bump_formatStr(BumpAlloc* arena, const char* fmt, ...);

#ifdef BASE_IMPL

#include <assert.h>
#include <malloc.h>
#include <memory.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

BumpAlloc bump_init(int64_t size, void* memory, const char* name) {
    BumpAlloc a;
    a.name = name;
    a.reserved = size;
    a.start = memory;
    bump_clear(&a);
    return a;
}

BumpAlloc bump_allocate(int64_t size, const char* name) {
    BumpAlloc a;
    a.name = name;
    a.reserved = size;
    a.start = malloc(size);
    assert(a.start);
    bump_clear(&a);
    return a;
}

void bump_free(BumpAlloc* a) {
    a->reserved = 0;
    free(a->start);
    a->start = NULL;
    a->end = NULL;
}

// TODO: push aligned + benchmark if it helps
void* bump_push(BumpAlloc* a, int64_t size) {
    char* o = (char*)(a->end);
    if (!(o + size < (char*)(a->start) + a->reserved)) {
        printf("[bump_push]: Out of bounds for allocator '%s'\n", a->name);
        assert(false);
    }
    a->end = o + size;
    return o;
}

void bump_pop(BumpAlloc* a, int64_t size) {
    char* c = (char*)(a->end);
    assert(size <= (c - (char*)(a->start)));
    a->end = c - size;
    memset(a->end, 0, size);
}

void bump_clear(BumpAlloc* a) {
    a->end = a->start;
    memset(a->start, 0, a->reserved);
}

char* bump_formatStr(BumpAlloc* arena, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    uint64_t len = vsnprintf(NULL, 0, fmt, args);
    char* out = BUMP_PUSH_ARR(arena, len + 1, char);
    vsprintf_s(out, len + 1, fmt, args);

    va_end(args);
    return out;
}

#endif