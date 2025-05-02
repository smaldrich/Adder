#include "snooze.h"
#include "ser.h"

typedef struct {
    uint64_t startTick;
    uint64_t endTick;
    uint64_t parentIdx;

    const char* file;
    uint64_t line;
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
    prof_Sample* firstSample;
} _prof_globs;

void _prof_callStart(const char* file, uint64_t line) {
    prof_Sample* sample = SNZ_ARENA_PUSH(_prof_globs.arena, prof_Sample);
    *sample = (prof_Sample){
        .startTick = SDL_GetPerformanceCounter(),
        .parentIdx = (uint64_t)(_prof_globs.activeSample - _prof_globs.firstSample),
        .file = file,
        .line = line,
    };
    _prof_globs.activeSample = sample;
}

void _prof_callEnd() {
    _prof_globs.activeSample->endTick = SDL_GetPerformanceCounter();
    prof_Sample* parent = &_prof_globs.firstSample[_prof_globs.activeSample->parentIdx];
    _prof_globs.activeSample = parent;
}

#define PROF_CALL(line) \
    do { \
        _prof_callStart(__FILE__, __LINE__); \
        line; \
        _prof_callEnd(); \
    } while(0)

// put before a block to profile it - changes block into a for loop, which does the defered call to callEnd
#define PROF_BLOCK() for(int _i_ = (_prof_callStart(__FILE__, __LINE__), 0); !_i_; (_prof_callEnd(), _i_++))

void prof_start(snz_Arena* arena) {
    _prof_globs.arena = arena;
    SNZ_ARENA_ARR_BEGIN(arena, prof_Sample);
    _prof_globs.firstSample = arena->start;
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