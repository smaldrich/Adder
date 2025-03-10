#include "snooze.h"
/*

open qs:
    perf? awful?
    hashmaps? or do we just punt that to userland?

API:
    ser_tBase()
    ser_tStruct()
    ser_tEnum()
    ser_tPtr()
    ser_tArray()

    ser_begin(arena) -> spec
    ser_addEnum(spec)
    ser_addStruct(spec, T)
    ser_addStructField(spec, ser_specT T, int offset)
    ser_end(spec) -> asserts on any failure

    ser_write(spec, F, T, obj) -> (err)
    ser_read(spec, F, T, arena) -> (err | obj*)
    ser_writeJSON(spec, F, T, obj)
    ser_readJSON(spec, F, T, arena)

FILE SIDE:
    a spec with a named structure of everything in the file
*/

typedef enum {
    SER_TK_INVALID,

    SER_TK_OTHER,
    SER_TK_PTR,
    SER_TK_ARRAY,

    SER_TK_INT8,
    SER_TK_INT16,
    SER_TK_INT32,
    SER_TK_INT64,
    SER_TK_UINT8,
    SER_TK_UINT16,
    SER_TK_UINT32,
    SER_TK_UINT64,

    SER_TK_FLOAT32,
    SER_TK_FLOAT64,
} ser_TKind;

typedef struct ser_T ser_T;
struct ser_T {
    ser_TKind kind;
    ser_T* inner;
    const char* referencedName;
};

typedef struct ser_SpecField ser_SpecField;
struct ser_SpecField {
    ser_SpecField* next;
    const char* tag;

    ser_T* type;
    int offsetInStruct;
};

typedef struct ser_SpecStruct ser_SpecStruct;
struct ser_SpecStruct {
    ser_SpecStruct* next;
    const char* tag;

    ser_SpecField* firstField;
};

typedef struct {
    const char* tag;

    const char** ids;
    int* idValues;
    int idCount;
} ser_SpecEnum;

typedef struct {
    bool validated; // when true indicates all specs have been added and that no more will, also that the entire thing has been validated.
    ser_SpecStruct* firstStructSpec;
    ser_SpecEnum* firstEnumSpec;

    snz_Arena* arena;
    ser_SpecStruct* activeStructSpec;
} ser_Spec;

static void _ser_specPushActiveStructSpecIfAny(ser_Spec* spec) {
    if (spec->activeStructSpec) {
        spec->activeStructSpec->next = spec->firstStructSpec;
        spec->firstStructSpec = spec->activeStructSpec;
        spec->activeStructSpec = NULL;
    }
}

static void _ser_specAssertUnlocked(ser_Spec* spec) {
    SNZ_ASSERT(!spec->validated, "Spec has already been validated.");
}

ser_T* ser_tBase(ser_Spec* s, ser_TKind kind) {
    _ser_specAssertUnlocked(s);
    SNZ_ASSERT(kind != SER_TK_INVALID, "Kind of 0 (SER_TK_INVALID) isn't a base kind.");
    SNZ_ASSERT(kind != SER_TK_OTHER, "Kind of 1 (SER_TK_OTHER) isn't a base kind.");
    SNZ_ASSERT(kind != SER_TK_PTR, "Kind of 2 (SER_TK_PTR) isn't a base kind.");
    SNZ_ASSERT(kind != SER_TK_ARRAY, "Kind of 3 (SER_TK_ARRAY) isn't a base kind.");
    ser_T* out = SNZ_ARENA_PUSH(s->arena, ser_T);
    out->kind = kind;
    return out;
}

#define ser_tStruct(specPtr, T) _ser_tStruct(specPtr, #T)
ser_T* _ser_tStruct(ser_Spec* s, const char* name) {
    _ser_specAssertUnlocked(s);
    ser_T* out = SNZ_ARENA_PUSH(s->arena, ser_T);
    out->kind = SER_TK_OTHER;
    out->referencedName = name;
    return out;
}

#define ser_tEnum(specPtr, T) _ser_tEnum(specPtr, #T)
ser_T* _ser_tEnum(ser_Spec* s, const char* name) {
    _ser_specAssertUnlocked(s);
    ser_T* out = SNZ_ARENA_PUSH(s->arena, ser_T);
    out->kind = SER_TK_OTHER;
    out->referencedName = name;
    return out;
}

ser_T* ser_tPtr(ser_Spec* s, ser_T* innerT) {
    _ser_specAssertUnlocked(s);
    ser_T* out = SNZ_ARENA_PUSH(s->arena, ser_T);
    out->kind = SER_TK_PTR;
    SNZ_ASSERT(innerT, "Can't point to a null type.");
    out->inner = innerT;
    return out;
}

ser_T* ser_tArray(ser_Spec* s, ser_T* innerT) {
    _ser_specAssertUnlocked(s);
    ser_T* out = SNZ_ARENA_PUSH(s->arena, ser_T);
    out->kind = SER_TK_ARRAY;
    SNZ_ASSERT(innerT, "Can't point to a null type.");
    out->inner = innerT;
    return out;
}

ser_Spec ser_begin(snz_Arena* arena) {
    ser_Spec out = (ser_Spec){
        .arena = arena,
        .validated = false,
    };
    return out;
}

void ser_addEnum(ser_Spec* s) {
    _ser_specAssertUnlocked(s);
    // FIXME: impl
    SNZ_ASSERT(false, "no impl :(");
}

#define ser_addStruct(specPtr, T) _ser_addStruct(specPtr, #T)
void _ser_addStruct(ser_Spec* spec, const char* name) {
    _ser_specAssertUnlocked(spec);
    _ser_specPushActiveStructSpecIfAny(spec);
    ser_SpecStruct* s = SNZ_ARENA_PUSH(spec->arena, ser_SpecStruct);
    spec->activeStructSpec = s;
    s->tag = name;
}

// FIXME: assert that active struct name == struct name?
#define ser_addStructField(specPtr, kindPtr, structName, name) \
    _ser_addStructField(specPtr, kind, name, offsetof(structName, name))
void _ser_addStructField(ser_Spec* spec, ser_T* kind, const char* tag, int offsetIntoStruct) {
    _ser_specAssertUnlocked(spec);
    SNZ_ASSERT(spec->activeStructSpec, "There was no active struct to add a field to.");
}

void ser_end(ser_Spec* spec) {
    _ser_specAssertUnlocked(spec);
    _ser_specPushActiveStructSpecIfAny(spec);
    spec->validated = true;
}

void main() {
    snz_Arena testArena = snz_arenaInit(10000000, "test arena");
    ser_Spec spec = ser_begin(&testArena);
    ser_Spec* s = &spec;

    ser_addStruct(s, HMM_Vec2);
    ser_addStructField(s, );
}