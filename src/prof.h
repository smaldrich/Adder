#include "snooze.h"
#include "ser.h"

typedef struct {
    uint64_t startTick;
    uint64_t endTick;
    uint64_t parentIdx;
    const char* line;
} prof_Sample;

SNZ_SLICE(prof_Sample);

void prof_registerSerSpecs() {
    ser_addStruct(prof_Sample, false);
    ser_addStructField(prof_Sample, ser_tBase(SER_TK_UINT64), startTick);
    ser_addStructField(prof_Sample, ser_tBase(SER_TK_UINT64), endTick);

    ser_addStruct(prof_SampleSlice, false);
    ser_addStructFieldSlice(prof_SampleSlice, prof_Sample, elems, count);
}

struct {
    snz_Arena* arena;
    prof_Sample* activeSample;
} _prof_globs;

void _prof_callStart(const char* line) {
    prof_Sample* sample = SNZ_ARENA_PUSH(_prof_globs.arena, prof_Sample);
    *sample = (prof_Sample){
        .startTick = SDL_GetPerformanceCounter(),
        .line = line,
        .parentIdx = ,
    };
    _prof_globs.activeSample = sample;
}

void _prof_callEnd() {
    _prof_globs.activeSample = _prof_globs.activeSample->parent;
    _prof_globs.activeSample->endTick = SDL_GetPerformanceCounter();
}

#define PROF_CALL(fn) \
    do { \
        _prof_callStart(#fn); \
        fn; \
        _prof_callEnd(); \
    } while (0)

void prof_start(snz_Arena* arena) {
    _prof_globs.arena = arena;
    SNZ_ARENA_ARR_BEGIN(arena, prof_Sample);
}

prof_SampleSlice prof_end() {
    prof_SampleSlice samples = SNZ_ARENA_ARR_END(_prof_globs.arena, prof_Sample);
    memset(&_prof_globs, 0, sizeof(_prof_globs));
    return samples;
}

void prof_sampleSliceBuild(prof_SampleSlice samples) {
    snzu_boxNew("sample build");
    snzu_boxFillParent();
    snzu_boxScope() {
    }
}