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

    SER_TK_STRUCT,
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

    SER_TK_COUNT,
} ser_TKind;

const char* ser_tKindStrs[] = {
    "SER_TK_INVALID",
    "SER_TK_STRUCT",
    "SER_TK_PTR",
    "SER_TK_ARRAY",

    "SER_TK_INT8",
    "SER_TK_INT16",
    "SER_TK_INT32",
    "SER_TK_INT64",
    "SER_TK_UINT8",
    "SER_TK_UINT16",
    "SER_TK_UINT32",
    "SER_TK_UINT64",
    "SER_TK_FLOAT32",
    "SER_TK_FLOAT64",
};

typedef struct ser_T ser_T;
typedef struct ser_SpecField ser_SpecField;
typedef struct ser_SpecStruct ser_SpecStruct;

struct ser_T {
    ser_TKind kind;
    ser_T* inner;
    const char* referencedName;
    ser_SpecStruct* referencedStruct;
};

struct ser_SpecField {
    ser_SpecField* next;
    const char* tag;

    ser_T* type;
    int offsetInStruct;
};

struct ser_SpecStruct {
    ser_SpecStruct* next;
    const char* tag;
    int indexIntoSpec;

    ser_SpecField* firstField;
    int fieldCount;
};

static struct {
    bool validated; // when true indicates all specs have been added and that no more will, also that the entire thing has been validated.
    ser_SpecStruct* firstStructSpec;
    int structSpecCount;

    snz_Arena* arena;
    ser_SpecStruct* activeStructSpec;
} _ser_globs;

static void _ser_pushActiveStructSpecIfAny() {
    if (_ser_globs.activeStructSpec) {
        _ser_globs.structSpecCount++;
        _ser_globs.activeStructSpec->next = _ser_globs.firstStructSpec;
        _ser_globs.firstStructSpec = _ser_globs.activeStructSpec;
        _ser_globs.activeStructSpec = NULL;
    }
}

static void _ser_assertInstanceValidForAddingToSpec() {
    SNZ_ASSERT(!_ser_globs.validated, "ser_begin hasn't been called yet.");
    SNZ_ASSERT(!_ser_globs.validated, "ser_end already called.");
}

static void _ser_assertInstanceValidated() {
    SNZ_ASSERT(_ser_globs.validated, "ser_end hasn't been called yet.");
}

ser_T* ser_tBase(ser_TKind kind) {
    _ser_assertInstanceValidForAddingToSpec();
    SNZ_ASSERT(kind != SER_TK_INVALID, "Kind of 0 (SER_TK_INVALID) isn't a base kind.");
    SNZ_ASSERT(kind != SER_TK_STRUCT, "Kind of 1 (SER_TK_STRUCT) isn't a base kind.");
    SNZ_ASSERT(kind != SER_TK_PTR, "Kind of 2 (SER_TK_PTR) isn't a base kind.");
    SNZ_ASSERT(kind != SER_TK_ARRAY, "Kind of 3 (SER_TK_ARRAY) isn't a base kind.");
    ser_T* out = SNZ_ARENA_PUSH(_ser_globs.arena, ser_T);
    out->kind = kind;
    return out;
}

#define ser_tStruct(T) _ser_tStruct(#T)
ser_T* _ser_tStruct(const char* name) {
    _ser_assertInstanceValidForAddingToSpec();
    ser_T* out = SNZ_ARENA_PUSH(_ser_globs.arena, ser_T);
    out->kind = SER_TK_STRUCT;
    out->referencedName = name;
    return out;
}

ser_T* ser_tPtr(ser_T* innerT) {
    _ser_assertInstanceValidForAddingToSpec();
    ser_T* out = SNZ_ARENA_PUSH(_ser_globs.arena, ser_T);
    out->kind = SER_TK_PTR;
    SNZ_ASSERT(innerT, "Can't point to a null type.");
    out->inner = innerT;
    return out;
}

ser_T* ser_tArray(ser_T* innerT) {
    _ser_assertInstanceValidForAddingToSpec();
    ser_T* out = SNZ_ARENA_PUSH(_ser_globs.arena, ser_T);
    out->kind = SER_TK_ARRAY;
    SNZ_ASSERT(innerT, "Can't point to a null type.");
    out->inner = innerT;
    return out;
}

#define ser_addStruct(T) _ser_addStruct(#T)
void _ser_addStruct(const char* name) {
    _ser_assertInstanceValidForAddingToSpec();
    _ser_pushActiveStructSpecIfAny();
    ser_SpecStruct* s = SNZ_ARENA_PUSH(_ser_globs.arena, ser_SpecStruct);
    _ser_globs.activeStructSpec = s;
    s->tag = name;
}

// FIXME: assert that active struct name == struct name?
// FIXME: assert that size of kind is same as sizeof prop named
#define ser_addStructField(kindPtr, structName, name) \
    _ser_addStructField(kindPtr, #name, offsetof(structName, name))
void _ser_addStructField(ser_T* kind, const char* tag, int offsetIntoStruct) {
    _ser_assertInstanceValidForAddingToSpec();
    SNZ_ASSERT(_ser_globs.activeStructSpec, "There was no active struct to add a field to.");

    ser_SpecField* field = SNZ_ARENA_PUSH(_ser_globs.arena, ser_SpecField);
    *field = (ser_SpecField){
        .offsetInStruct = offsetIntoStruct,
        .tag = tag,
        .type = kind,
        .next = _ser_globs.activeStructSpec->firstField,
    };
    _ser_globs.activeStructSpec->firstField = field;
    _ser_globs.activeStructSpec->fieldCount++;
}

void ser_begin(snz_Arena* arena) {
    _ser_globs.arena = arena;
    _ser_globs.validated = false;
}

ser_SpecStruct* _ser_getStructSpecByName(const char* name) {
    for (ser_SpecStruct* s = _ser_globs.firstStructSpec; s; s = s->next) {
        if (strcmp(s->tag, name) == 0) {
            return s;
        }
    }
    return NULL;
}

bool _ser_kindNonTerminal(ser_TKind kind) {
    SNZ_ASSERTF(kind > SER_TK_INVALID || kind < SER_TK_COUNT, "invalid kind '%d'", kind);
    if (kind == SER_TK_PTR) {
        return true;
    } else if (kind == SER_TK_ARRAY) {
        return true;
    }
    return false;
}

void ser_end() {
    _ser_assertInstanceValidForAddingToSpec();
    _ser_pushActiveStructSpecIfAny();

    // FIXME: could double check that offsets are within the size of a given struct
    int i = 0;
    // FIXME: duplicates check for structs and fields
    for (ser_SpecStruct* s = _ser_globs.firstStructSpec; s; s = s->next) {
        // FIXME: good error reporting here, trace of type, line no., etc.
        SNZ_ASSERT(s->tag, "struct with no tag.");
        for (ser_SpecField* f = s->firstField; f; f = f->next) {
            SNZ_ASSERT(f->tag, "struct field with no tag.");
            for (ser_T* inner = f->type; inner; inner = inner->inner) {
                if (inner->kind == SER_TK_STRUCT) {
                    inner->referencedStruct = _ser_getStructSpecByName(inner->referencedName);
                    SNZ_ASSERTF(inner->referencedStruct, "no definition with the name '%s' found.", inner->referencedName);
                }

                if (_ser_kindNonTerminal(inner->kind)) {
                    SNZ_ASSERT(inner->inner, "non-terminal kind with no inner kind");
                } else {
                    SNZ_ASSERT(!inner->inner, "terminal kind with an inner kind");
                }
            } // end validating inners on field
        } // end fields

        s->indexIntoSpec = i;
        i++;
    } // end all structs
    _ser_globs.validated = true;
}

void _ser_printSpec() {
    for (ser_SpecStruct* s = _ser_globs.firstStructSpec; s; s = s->next) {
        printf("%s:\n", s->tag);
        for (ser_SpecField* f = s->firstField; f; f = f->next) {
            printf("\t%s:\n", f->tag);
            for (ser_T* inner = f->type; inner; inner = inner->inner) {
                const char* str = ser_tKindStrs[inner->kind];
                printf("\t\t%s\n", str);
            }
        }
    }
}

typedef struct _ser_QueuedStruct _ser_QueuedStruct;
typedef struct {
    _ser_QueuedStruct* next;
    ser_SpecStruct* spec;
    void* obj;
} _ser_QueuedStruct;

typedef struct {
    _ser_QueuedStruct* nextStruct;
    // address -> loc in file table
    // locations of loc stubs in file
    FILE* file;
} _ser_WriteInst;

void _ser_writeField(_ser_WriteInst* write, ser_SpecField* field, void* obj, int indentLevel) {
    ser_TKind kind = field->type->kind;
    if (kind == SER_TK_PTR) {
        // FIXME: dedup deps
        fprintf(write->file, "no idea what to do w this one tbh.\n");
        return;
    } else if (kind == SER_TK_ARRAY) {
        fprintf(write->file, "no idea what to do w this one tbh.\n");
        return;
    } else if (kind == SER_TK_STRUCT) {
        ser_SpecStruct* s = field->type->referencedStruct;
        void* innerStruct = (char*)obj + field->offsetInStruct;
        for (ser_SpecField* innerField = s->firstField; innerField; innerField = innerField->next) {
            _ser_writeField(write, innerField, innerStruct, indentLevel + 1);
        }
        return;
    } else {
        SNZ_ASSERTF(false, "unreachable. kind: %d.", kind);
    }

    // base types
    fprintf(write->file, "%*s", indentLevel * 2, "");

    void* ptr = ((char*)obj) + field->offsetInStruct;
    if (kind == SER_TK_FLOAT32) {
        fprintf(write->file, "%f", *(float*)ptr);
    } else if (kind == SER_TK_FLOAT64) {
        fprintf(write->file, "%f", *(double*)ptr);
    } else if (kind == SER_TK_INT8) {
        fprintf(write->file, "%d", *(int8_t*)ptr);
    } else if (kind == SER_TK_INT16) {
        fprintf(write->file, "%d", *(int16_t*)ptr);
    } else if (kind == SER_TK_INT32) {
        fprintf(write->file, "%d", *(int32_t*)ptr);
    } else if (kind == SER_TK_INT64) {
        fprintf(write->file, "%lld", *(int64_t*)ptr);
    } else if (kind == SER_TK_UINT8) {
        fprintf(write->file, "%u", *(uint8_t*)ptr);
    } else if (kind == SER_TK_UINT16) {
        fprintf(write->file, "%u", *(uint16_t*)ptr);
    } else if (kind == SER_TK_UINT32) {
        fprintf(write->file, "%u", *(uint32_t*)ptr);
    } else if (kind == SER_TK_UINT64) {
        fprintf(write->file, "%llu", *(uint64_t*)ptr);
    }
    fprintf(write->file, "\n");
}

#define ser_write(F, T, obj, scratch) _ser_write(F, #T, obj, scratch)
void _ser_write(FILE* f, const char* typename, void* obj, snz_Arena* scratch) {
    _ser_assertInstanceValidated();

    _ser_WriteInst write = { 0 };
    write.file = f;

    write.nextStruct = SNZ_ARENA_PUSH(scratch, _ser_QueuedStruct);
    write.nextStruct->obj = obj;
    write.nextStruct->spec = _ser_getStructSpecByName(typename);
    SNZ_ASSERTF(write.nextStruct->spec, "No definition for struct '%s'", typename);

    { // writing spec
        fprintf(f, "beginning spec\n");
        fprintf(f, "decl count: %d\n", _ser_globs.structSpecCount);
        for (ser_SpecStruct* s = _ser_globs.firstStructSpec; s; s = s->next) {
            fprintf(f, "\tname: %lld %s\n", strlen(s->tag), s->tag);

            fprintf(f, "\tfield count: %d\n", s->fieldCount);
            for (ser_SpecField* field = s->firstField; field; field = field->next) {
                fprintf(f, "\t\tname: %lld %s\n", strlen(field->tag), field->tag);
                for (ser_T* inner = field->type; inner; inner = inner->inner) {
                    fprintf(f, "\t\t\tkind: %d\n", inner->kind);
                    if (inner->kind == SER_TK_STRUCT) {
                        fprintf(f, "\t\t\tpointing at: %d\n", inner->referencedStruct->indexIntoSpec);
                    }
                }
            }
        }
    } // end writing spec

    // write all structs and their deps
    while (write.nextStruct) { // FIXME: cutoff
        _ser_QueuedStruct* s = write.nextStruct;
        write.nextStruct = s->next;

        fprintf(f, "struct, kind: %d\n", s->spec->indexIntoSpec);
        for (ser_SpecField* field = s->spec->firstField; field; field = field->next) {
            _ser_writeField(&write, field, obj, 1);
        }
    }
}

#include "geometry.h"

void ser_tests() {
    snz_testPrintSection("ser 3");
    snz_Arena testArena = snz_arenaInit(10000000, "test arena");

    ser_begin(&testArena);

    ser_addStruct(HMM_Vec2);
    ser_addStructField(ser_tBase(SER_TK_FLOAT32), HMM_Vec2, X);
    ser_addStructField(ser_tBase(SER_TK_FLOAT32), HMM_Vec2, Y);

    ser_addStruct(HMM_Vec3);
    ser_addStructField(ser_tStruct(HMM_Vec2), HMM_Vec3, XY);
    ser_addStructField(ser_tBase(SER_TK_FLOAT32), HMM_Vec3, Z);

    ser_addStruct(geo_Tri);
    ser_addStructField(ser_tStruct(HMM_Vec3), geo_Tri, a);
    ser_addStructField(ser_tStruct(HMM_Vec3), geo_Tri, b);
    ser_addStructField(ser_tStruct(HMM_Vec3), geo_Tri, c);

    ser_addStruct(geo_TriSlice);
    ser_addStructField(ser_tArray(ser_tStruct(geo_Tri)), geo_TriSlice, elems);

    ser_addStruct(geo_Line);
    ser_addStructField(ser_tStruct(HMM_Vec3), geo_Line, a);
    ser_addStructField(ser_tStruct(HMM_Vec3), geo_Line, b);

    ser_addStruct(geo_LineSlice);
    ser_addStructField(ser_tArray(ser_tStruct(geo_Line)), geo_LineSlice, elems);

    ser_end();

    FILE* f = fopen("testing/ser3.adder", "w");
    geo_Tri t = (geo_Tri){
        .a = HMM_V3(0, 1, 2),
        .b = HMM_V3(3, 4, 5),
        .c = HMM_V3(6, 7, 8),
    };
    ser_write(f, geo_Tri, &t, &testArena);
    // _ser_printSpec();
    fclose(f);
}