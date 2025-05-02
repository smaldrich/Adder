#pragma once

#include "snooze.h"
#include "settings.h"
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

#define SER_VERSION 0

typedef enum {
    SER_TK_INVALID,

    SER_TK_STRUCT,
    SER_TK_PTR,
    SER_TK_SLICE,
    SER_TK_CSTRING,

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
    "SER_TK_SLICE",
    "SER_TK_CSTRING",

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

int64_t _ser_tKindSizes[] = {
    [SER_TK_INT8] = sizeof(int8_t),
    [SER_TK_INT16] = sizeof(int16_t),
    [SER_TK_INT32] = sizeof(int32_t),
    [SER_TK_INT64] = sizeof(int64_t),
    [SER_TK_UINT8] = sizeof(uint8_t),
    [SER_TK_UINT16] = sizeof(uint16_t),
    [SER_TK_UINT32] = sizeof(uint32_t),
    [SER_TK_UINT64] = sizeof(uint64_t),

    [SER_TK_FLOAT32] = sizeof(float),
    [SER_TK_FLOAT64] = sizeof(double),
};

typedef struct _ser_T _ser_T;
typedef struct _ser_SpecField _ser_SpecField;
typedef struct _ser_SpecStruct _ser_SpecStruct;

struct _ser_T {
    ser_TKind kind;
    _ser_T* inner; // FIXME: this isn't actually reqd, please remove :)

    const char* referencedName;
    int64_t referencedIndex;
    _ser_SpecStruct* referencedStruct;
    int offsetOfSliceLengthIntoStruct;
};

struct _ser_SpecField {
    _ser_SpecField* next;
    const char* tag;

    _ser_T* type;
    int offsetInStruct;
    bool missingFromStruct;
};

struct _ser_SpecStruct {
    _ser_SpecStruct* next;
    const char* tag;
    int64_t indexIntoSpec;
    bool pointable;

    _ser_SpecField* firstField;
    _ser_SpecField* lastField;
    int64_t fieldCount;
    int64_t size;
};

static struct {
    bool validated; // when true indicates all specs have been added and that no more will, also that the entire thing has been validated.
    _ser_SpecStruct* firstStructSpec; // FIXME: enums too
    int64_t structSpecCount;

    snz_Arena* specArena;
    _ser_SpecStruct* activeStructSpec;
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

_ser_T* ser_tBase(ser_TKind kind) {
    _ser_assertInstanceValidForAddingToSpec();
    SNZ_ASSERT(kind != SER_TK_INVALID, "Kind of 0 (SER_TK_INVALID) isn't a base kind.");
    SNZ_ASSERT(kind != SER_TK_STRUCT, "Kind of 1 (SER_TK_STRUCT) isn't a base kind.");
    SNZ_ASSERT(kind != SER_TK_PTR, "Kind of 2 (SER_TK_PTR) isn't a base kind.");
    SNZ_ASSERT(kind != SER_TK_SLICE, "Kind of 3 (SER_TK_SLICE) isn't a base kind.");
    _ser_T* out = SNZ_ARENA_PUSH(_ser_globs.specArena, _ser_T);
    out->kind = kind;
    return out;
}

#define ser_tStruct(T) _ser_tStruct(#T)
_ser_T* _ser_tStruct(const char* name) {
    _ser_assertInstanceValidForAddingToSpec();
    _ser_T* out = SNZ_ARENA_PUSH(_ser_globs.specArena, _ser_T);
    out->kind = SER_TK_STRUCT;
    out->referencedName = name;
    return out;
}

#define ser_tPtr(T) _ser_tPtr(#T)
_ser_T* _ser_tPtr(const char* innerName) {
    _ser_assertInstanceValidForAddingToSpec();
    _ser_T* out = SNZ_ARENA_PUSH(_ser_globs.specArena, _ser_T);
    out->kind = SER_TK_PTR;
    out->inner = _ser_tStruct(innerName);
    return out;
}

#define ser_addStruct(T, pointable) _ser_addStruct(#T, sizeof(T), pointable)
void _ser_addStruct(const char* name, int64_t size, bool pointable) {
    _ser_assertInstanceValidForAddingToSpec();
    _ser_pushActiveStructSpecIfAny();
    _ser_SpecStruct* s = SNZ_ARENA_PUSH(_ser_globs.specArena, _ser_SpecStruct);
    _ser_globs.activeStructSpec = s;
    s->tag = name;
    s->size = size;
    s->pointable = pointable;
}

// FIXME: assert that size of kind is same as sizeof prop named
#define ser_addStructField(structT, type, name) \
    _ser_addStructField(#structT, type, #name, offsetof(structT, name))
void _ser_addStructField(const char* activeStructName, _ser_T* type, const char* name, int offsetIntoStruct) {
    _ser_assertInstanceValidForAddingToSpec();
    SNZ_ASSERT(_ser_globs.activeStructSpec, "There was no active struct to add a field to.");
    SNZ_ASSERTF(strcmp(_ser_globs.activeStructSpec->tag, activeStructName) == 0,
        "Active struct '%s' wasn't the same as expected struct '%s'\n",
        _ser_globs.activeStructSpec->tag, activeStructName);

    _ser_SpecField* field = SNZ_ARENA_PUSH(_ser_globs.specArena, _ser_SpecField);
    *field = (_ser_SpecField){
        .offsetInStruct = offsetIntoStruct,
        .tag = name,
        .type = type,
    };
    if (_ser_globs.activeStructSpec->lastField) {
        _ser_globs.activeStructSpec->lastField->next = field;
    } else {
        _ser_globs.activeStructSpec->firstField = field;
    }
    _ser_globs.activeStructSpec->lastField = field;
    _ser_globs.activeStructSpec->fieldCount++;
}

// FIXME: assert that prop for length is an int64
#define ser_addStructFieldSlice(structT, innerT, ptrName, lengthName) \
    _ser_addStructFieldSlice(#structT, #innerT, #ptrName, offsetof(structT, ptrName), offsetof(structT, lengthName))
void _ser_addStructFieldSlice(
    const char* activeStructName,
    const char* innerTypeName,
    const char* tag,
    int ptrOffsetIntoStruct,
    int lengthOffsetIntoStruct) {
    _ser_T* type = SNZ_ARENA_PUSH(_ser_globs.specArena, _ser_T);
    *type = (_ser_T){
        .inner = _ser_tStruct(innerTypeName),
        .kind = SER_TK_SLICE,
        .offsetOfSliceLengthIntoStruct = lengthOffsetIntoStruct,
    };
    _ser_addStructField(activeStructName, type, tag, ptrOffsetIntoStruct);
}

void ser_begin(snz_Arena* specArena) {
    _ser_globs.specArena = specArena;
    _ser_globs.validated = false;
}

_ser_SpecStruct* _ser_getStructSpecByName(const char* name) {
    for (_ser_SpecStruct* s = _ser_globs.firstStructSpec; s; s = s->next) {
        if (strcmp(s->tag, name) == 0) {
            return s;
        }
    }
    return NULL;
}

_ser_SpecField* _ser_getFieldSpecByName(_ser_SpecStruct* struc, const char* name) {
    for (_ser_SpecField* f = struc->firstField; f; f = f->next) {
        if (strcmp(f->tag, name) == 0) {
            return f;
        }
    }
    return NULL;
}

// return indicates failure.
bool _ser_validate(_ser_SpecStruct* firstStruct) {
    // FIXME: could double check that offsets are within the size of a given struct
    int i = 0;
    // FIXME: duplicates check for structs and fields
    for (_ser_SpecStruct* s = firstStruct; s; s = s->next) {
        for (_ser_SpecStruct* other = s->next; other; other = other->next) {
            if (strcmp(other->tag, s->tag) == 0) {
                return false;
            }
        }
        // FIXME: good error reporting here, trace of type, line no., etc.
        SNZ_ASSERT(s->tag && strlen(s->tag), "struct with no tag.");
        for (_ser_SpecField* f = s->firstField; f; f = f->next) {
            SNZ_ASSERT(f->tag && strlen(f->tag), "struct field with no tag.");
            SNZ_ASSERT(f->type, "can't have a field with no type.");

            for (_ser_T* inner = f->type; inner; inner = inner->inner) {
                SNZ_ASSERTF(inner->kind > SER_TK_INVALID && inner->kind < SER_TK_COUNT, "invalid kind: %d.", inner->kind);
                if (inner->kind == SER_TK_STRUCT) {
                    // lookup by name if that exists, otherwise by index
                    if (inner->referencedName && strlen(inner->referencedName)) {
                        inner->referencedStruct = _ser_getStructSpecByName(inner->referencedName);
                        SNZ_ASSERTF(inner->referencedStruct, "no definition with the name '%s' found.", inner->referencedName);
                    } else {
                        // FIXME: O(N) here is shitty
                        inner->referencedStruct = firstStruct;
                        for (int i = 0; i < inner->referencedIndex; i++) {
                            inner->referencedStruct = inner->referencedStruct->next;
                            // FIXME: just segfaults on an OOBs err here, fix that
                        }
                    }

                    if (inner == f->type) {
                        SNZ_ASSERT(inner->referencedStruct != s, "can't nest structs. make the field a ptr or ptr view.");
                    }
                } else if (inner->kind == SER_TK_PTR) {
                    SNZ_ASSERT(inner->inner, "ptr with no inner kind");
                    SNZ_ASSERT(inner->inner->kind == SER_TK_STRUCT, "ptr that doesn't point to struct type");
                } else if (inner->kind == SER_TK_SLICE) {
                    SNZ_ASSERT(inner->inner, "slice with no inner kind");
                    SNZ_ASSERT(inner->inner->kind == SER_TK_STRUCT, "slice that doesn't point to struct type");
                } else {
                    SNZ_ASSERT(!inner->inner, "terminal kind with an inner kind");
                }
            } // end validating inners on field

            for (_ser_T* inner = f->type; inner; inner = inner->inner) {
                if (inner->kind == SER_TK_PTR) {
                    SNZ_ASSERT(inner->inner->referencedStruct->pointable, "Ptr to a non-pointable type.");
                } else if (inner->kind == SER_TK_SLICE) {
                    SNZ_ASSERT(!inner->inner->referencedStruct->pointable, "Slice to a pointable type.");
                }
            } // end 2nd pass validating inners
        } // end fields

        s->indexIntoSpec = i;
        i++;
    } // end all structs

    return true;
}

void ser_end() {
    _ser_assertInstanceValidForAddingToSpec();
    _ser_pushActiveStructSpecIfAny();

    _ser_validate(_ser_globs.firstStructSpec);

    _ser_globs.validated = true;
}

typedef struct _ser_PtrTranslation _ser_PtrTranslation;
struct _ser_PtrTranslation {
    uint64_t key;
    uint64_t value;
    _ser_PtrTranslation* nextCollided;
};

#define _SER_PTR_TRANSLATION_BUCKET_COUNT 2048
#define _SER_PTR_TRANSLATION_MAX 2048
#define _SER_PTR_TRANSLATION_STUB_MAX 2048

typedef struct {
    _ser_PtrTranslation* translationBuckets[_SER_PTR_TRANSLATION_BUCKET_COUNT];

    struct {
        _ser_PtrTranslation elems[_SER_PTR_TRANSLATION_MAX];
        int64_t count;
    } translations;

    struct {
        uint64_t locationsToPatch[_SER_PTR_TRANSLATION_STUB_MAX];
        uint64_t keyOfValue[_SER_PTR_TRANSLATION_STUB_MAX];
        int64_t count;
    } stubs;
} _ser_PtrTranslationTable;
// FIXME: dyn sizing

// https://zimbry.blogspot.com/2011/09/better-bit-mixing-improving-on.html
uint64_t _ser_addressHash(uint64_t address) {
    address ^= (address >> 33);
    address *= 0xff51afd7ed558ccd;
    address ^= (address >> 33);
    address *= 0xc4ceb9fe1a85ec53;
    address ^= (address >> 33);
    return address;
}

// return indicates whether the key didn't exist before & and the val was added
bool _ser_ptrTranslationSet(_ser_PtrTranslationTable* table, uint64_t key, uint64_t value) {
    int64_t bucket = _ser_addressHash(key) % _SER_PTR_TRANSLATION_BUCKET_COUNT;
    _ser_PtrTranslation* initial = table->translationBuckets[bucket];
    for (_ser_PtrTranslation* node = initial; node; node = node->nextCollided) {
        if (node->key == key) {
            node->value = value;
            return false;
        }
    }

    SNZ_ASSERT(table->translations.count < _SER_PTR_TRANSLATION_MAX, "Too many addresses to serialize. see the fixme.");
    _ser_PtrTranslation* node = &table->translations.elems[table->translations.count];
    table->translations.count++;
    *node = (_ser_PtrTranslation){
        .key = key,
        .value = value,
        .nextCollided = initial,
    };
    table->translationBuckets[bucket] = node;
    return true;
}

// null if not in table
uint64_t* _ser_ptrTranslationGet(_ser_PtrTranslationTable* table, uint64_t key) {
    int64_t bucket = _ser_addressHash(key) % _SER_PTR_TRANSLATION_BUCKET_COUNT;
    for (_ser_PtrTranslation* node = table->translationBuckets[bucket]; node; node = node->nextCollided) {
        if (node->key == key) {
            return &node->value;
        }
    }
    return NULL;
}

void _ser_ptrTranslationStubAdd(_ser_PtrTranslationTable* table, uint64_t addressToPatchAt, uint64_t keyOfPatchValue) {
    SNZ_ASSERTF(table->stubs.count < _SER_PTR_TRANSLATION_STUB_MAX,
                "Too many ptr stubs: %d", table->stubs.count);

    table->stubs.locationsToPatch[table->stubs.count] = addressToPatchAt;
    table->stubs.keyOfValue[table->stubs.count] = keyOfPatchValue;
    table->stubs.count++;
}

typedef struct _serw_QueuedStruct _serw_QueuedStruct;
struct _serw_QueuedStruct {
    _serw_QueuedStruct* next;
    _ser_SpecStruct* spec;
    void* obj;
};

typedef enum {
    SER_WE_OK,
    SER_WE_WRITE_FAILED,
    SER_WE_PTR_PROBLEM,
} ser_WriteError;

typedef struct {
    _serw_QueuedStruct* nextStruct;
    _ser_PtrTranslationTable ptrTable;

    FILE* file;
    uint64_t positionIntoFile;
    snz_Arena* scratch;
} _serw_WriteInst;

bool _ser_isSystemLittleEndian() {
    uint64_t x = 1;
    return ((char*)&x)[0] == 1;
}

#define _SERW_WRITE_BYTES_OR_RETURN(w, src, size, swapWithEndianness) \
    do { \
        ser_WriteError e = _serw_writeBytes(w, src, size, swapWithEndianness); \
        if(e != SER_WE_OK) { \
            return e; \
        } \
    } while (0)

ser_WriteError _serw_writeBytes(_serw_WriteInst* write, const void* src, int64_t size, bool swapWithEndianness) {
    uint8_t* srcChars = (uint8_t*)src;
    if (swapWithEndianness && _ser_isSystemLittleEndian()) {
        uint8_t* buf = snz_arenaPush(write->scratch, size, 1);
        for (int64_t i = 0; i < size; i++) {
            buf[i] = srcChars[size - 1 - i];
        }
        int written = fwrite(buf, size, 1, write->file);
        if (written != 1) {
            return SER_WE_WRITE_FAILED;
        }
        snz_arenaPop(write->scratch, size);
    } else {
        int written = fwrite(src, size, 1, write->file);
        if (written != 1) {
            return SER_WE_WRITE_FAILED;
        }
    }

    // int count = (8 - (write->positionIntoFile % 8)) % 8;
    // write->positionIntoFile += count;
    // for (int i = 0; i < count; i++) {
    //     uint8_t byte = 0xFF;
    //     fwrite(&byte, 1, 1, write->file);
    // }

    write->positionIntoFile += size;
    return SER_WE_OK;
}

ser_WriteError _serw_writeField(_serw_WriteInst* write, _ser_SpecField* field, void* obj) {
    ser_TKind kind = field->type->kind;
    if (kind == SER_TK_PTR) {
        uint64_t pointed = (uint64_t) * (void**)((char*)obj + field->offsetInStruct);
        if (pointed != 0 && !_ser_ptrTranslationGet(&write->ptrTable, pointed)) {
            _serw_QueuedStruct* queued = SNZ_ARENA_PUSH(write->scratch, _serw_QueuedStruct);
            *queued = (_serw_QueuedStruct){
                .next = write->nextStruct,
                .obj = (void*)pointed,
                .spec = field->type->inner->referencedStruct,
            };
            write->nextStruct = queued;
            SNZ_ASSERT(_ser_ptrTranslationSet(&write->ptrTable, pointed, 0), "huh");
        } // FIXME: just put the location in file here if the object has already been written

        if (pointed != 0) {
            _ser_ptrTranslationStubAdd(&write->ptrTable, write->positionIntoFile, pointed);
        }

        // this is overwritten when the ptr is patched (if non-null)
        return _serw_writeBytes(write, &pointed, sizeof(void*), true);
    } else if (kind == SER_TK_SLICE) {
        uint64_t pointed = (uint64_t) * (void**)((char*)obj + field->offsetInStruct);
        int64_t length = *(int64_t*)((char*)obj + field->type->offsetOfSliceLengthIntoStruct);
        _SERW_WRITE_BYTES_OR_RETURN(write, &length, sizeof(length), true);

        _ser_SpecStruct* innerStructType = field->type->inner->referencedStruct;
        for (int64_t i = 0; i < length; i++) {
            uint64_t innerStructAddress = pointed + (i * innerStructType->size);
            for (_ser_SpecField* innerField = innerStructType->firstField; innerField; innerField = innerField->next) {
                ser_WriteError err = _serw_writeField(write, innerField, (void*)innerStructAddress);
                if (err != SER_WE_OK) {
                    return err;
                }
            }
        }
        return SER_WE_OK;
    } else if (kind == SER_TK_STRUCT) {
        _ser_SpecStruct* s = field->type->referencedStruct;
        void* innerStruct = (char*)obj + field->offsetInStruct;

        if (s->pointable) {
            _ser_ptrTranslationSet(&write->ptrTable, (uint64_t)innerStruct, write->positionIntoFile);
        }
        for (_ser_SpecField* innerField = s->firstField; innerField; innerField = innerField->next) {
            ser_WriteError err = _serw_writeField(write, innerField, innerStruct);
            if (err != SER_WE_OK) {
                return err;
            }
        }
        return SER_WE_OK;
    } else if (kind == SER_TK_CSTRING) {
        const char* string = *(char**)((char*)obj + field->offsetInStruct);

        if (string == NULL) {
            uint64_t zero = 0;
            _SERW_WRITE_BYTES_OR_RETURN(write, &zero, sizeof(uint64_t), false);
        } else {
            uint64_t length = strlen(string);
            _SERW_WRITE_BYTES_OR_RETURN(write, &length, sizeof(length), true);
            _SERW_WRITE_BYTES_OR_RETURN(write, string, length, false);
        }
        return SER_WE_OK;
    }


    void* ptr = ((char*)obj) + field->offsetInStruct;
    SNZ_ASSERT(kind > SER_TK_INVALID && kind < SER_TK_COUNT, "invalid kind?? after validation tho??");
    int size = _ser_tKindSizes[kind];
    SNZ_ASSERTF(size != 0, "Kind of %d had no associated size.", kind);
    return _serw_writeBytes(write, ptr, size, true);
}

// FIXME: typecheck of some kind on obj
#define ser_write(F, T, obj, scratch) _ser_write(F, #T, obj, scratch)
ser_WriteError _ser_write(FILE* f, const char* typename, void* seedObj, snz_Arena* scratch) {
    _ser_assertInstanceValidated();

    _serw_WriteInst write = { 0 };
    write.file = f;
    write.scratch = scratch;

    write.nextStruct = SNZ_ARENA_PUSH(scratch, _serw_QueuedStruct);
    write.nextStruct->obj = seedObj;
    write.nextStruct->spec = _ser_getStructSpecByName(typename);
    SNZ_ASSERTF(write.nextStruct->spec, "No definition for struct '%s'", typename);

    { // writing spec
        uint64_t version = SER_VERSION;
        _SERW_WRITE_BYTES_OR_RETURN(&write, &version, sizeof(version), true);
        _SERW_WRITE_BYTES_OR_RETURN(&write, &_ser_globs.structSpecCount, sizeof(_ser_globs.structSpecCount), true);  // decl count
        for (_ser_SpecStruct* s = _ser_globs.firstStructSpec; s; s = s->next) {
            uint64_t tagLen = strlen(s->tag);
            _SERW_WRITE_BYTES_OR_RETURN(&write, &tagLen, sizeof(tagLen), true); // length of tag
            _SERW_WRITE_BYTES_OR_RETURN(&write, s->tag, tagLen, false); // tag

            _SERW_WRITE_BYTES_OR_RETURN(&write, &s->fieldCount, sizeof(s->fieldCount), true); // field count
            for (_ser_SpecField* field = s->firstField; field; field = field->next) {
                uint64_t tagLen = strlen(field->tag);
                _SERW_WRITE_BYTES_OR_RETURN(&write, &tagLen, sizeof(tagLen), true); // length of tag
                _SERW_WRITE_BYTES_OR_RETURN(&write, field->tag, tagLen, false); // tag
                for (_ser_T* inner = field->type; inner; inner = inner->inner) {
                    uint8_t enumVal = (uint8_t)inner->kind;
                    _SERW_WRITE_BYTES_OR_RETURN(&write, &enumVal, 1, false);
                    if (inner->kind == SER_TK_STRUCT) {
                        int64_t idx = inner->referencedStruct->indexIntoSpec;
                        _SERW_WRITE_BYTES_OR_RETURN(&write, &idx, sizeof(idx), true);
                    }
                }
            }
        }
    } // end writing spec

    // write all structs and their deps
    while (write.nextStruct) { // FIXME: cutoff
        _serw_QueuedStruct* s = write.nextStruct;
        write.nextStruct = s->next;

        int64_t structIdx = s->spec->indexIntoSpec;
        _SERW_WRITE_BYTES_OR_RETURN(&write, &structIdx, sizeof(structIdx), true); // kind of structs

        if (s->spec->pointable) {
            uint64_t* got = _ser_ptrTranslationGet(&write.ptrTable, (uint64_t)s->obj);
            if (got) {
                SNZ_ASSERT(*got == (uint64_t)0, "AHHHHHH");
            } // assert not already written to file
            _ser_ptrTranslationSet(&write.ptrTable, (uint64_t)s->obj, write.positionIntoFile);
        }
        for (_ser_SpecField* field = s->spec->firstField; field; field = field->next) {
            ser_WriteError err = _serw_writeField(&write, field, s->obj);
            if (err != SER_WE_OK) {
                return err;
            }
        }
    }

    { // patch ptrs
        _ser_PtrTranslationTable* t = &write.ptrTable;
        for (int64_t i = 0; i < t->stubs.count; i++) {
            int64_t fileLoc = t->stubs.locationsToPatch[i];
            fseek(write.file, fileLoc, SEEK_SET);

            uint64_t* otherLoc = _ser_ptrTranslationGet(&write.ptrTable, t->stubs.keyOfValue[i]);
            if (!otherLoc) {
                return SER_WE_PTR_PROBLEM;
            }
            _SERW_WRITE_BYTES_OR_RETURN(&write, otherLoc, sizeof(*otherLoc), true);
        }
    }

    return SER_WE_OK;
}

typedef struct {
    _ser_PtrTranslationTable ptrTable;

    FILE* file;
    uint64_t positionIntoFile;

    snz_Arena* outArena;
    snz_Arena* scratch;
} _serr_ReadInst;

typedef enum {
    SER_RE_OK,
    SER_RE_WRONG_TYPE_OF_STRUCT,
    SER_RE_GARBAGE_SPEC,
    SER_RE_UNRECOVERABLE_SPEC_MISMATCH,
    SER_RE_READ_FAILED,
    SER_RE_PTR_PROBLEM,
    SER_RE_GARBAGE_DATA,
} ser_ReadError;

#define _SERR_READ_BYTES_OR_RETURN(read, out, size, swapWithEndianness) \
    do { \
        ser_ReadError err = _serr_readBytes(read, out, size, swapWithEndianness); \
        if(err != SER_RE_OK) { \
            SNZ_LOGF("Read bytes failed. File pos: %llu", (read)->positionIntoFile); \
            return err; \
        } \
    } while(0)

// if out is null, moves the file ptr forward without actually reading the bytes
ser_ReadError _serr_readBytes(_serr_ReadInst* read, void* out, int64_t size, bool swapWithEndianness) {
    ser_ReadError err = SER_RE_OK;
    if (!out) {
        if (fseek(read->file, size, SEEK_CUR)) {
            err = SER_RE_READ_FAILED;
        }
    } else if (swapWithEndianness) {
        uint8_t* bytes = snz_arenaPush(read->scratch, size, 1); // FIXME: static cache the space for these & just assert on size? - same w/ write?
        if (fread(bytes, size, 1, read->file) != 1) {
            err = SER_RE_READ_FAILED;
        }

        for (int64_t i = 0; i < size; i++) {
            *((uint8_t*)out + i) = bytes[size - 1 - i];
        }
        snz_arenaPop(read->scratch, size);
    } else {
        if (fread(out, size, 1, read->file) != 1) {
            err = SER_RE_READ_FAILED;
        }
    }
    read->positionIntoFile += size;
    return err;
}

// NULL obj indicates the field is not getting read to anywhere
// still gets executed to move forward within the file the correct amount tho
ser_ReadError _serr_readField(_serr_ReadInst* read, _ser_SpecField* field, void* obj) {
    void* outPos = NULL;
    if (!field->missingFromStruct && obj != NULL) {
        outPos = (char*)obj + field->offsetInStruct;
    }

    ser_TKind kind = field->type->kind;
    if (kind == SER_TK_PTR) {
        uint64_t loc = 0;
        _SERR_READ_BYTES_OR_RETURN(read, &loc, sizeof(void*), true);
        if (outPos != NULL && loc != 0) {
            _ser_ptrTranslationStubAdd(&read->ptrTable, (uint64_t)outPos, loc);
        }
        return SER_RE_OK;
    } else if (kind == SER_TK_SLICE) {
        _ser_SpecStruct* structSpec = field->type->inner->referencedStruct;

        int64_t count = 0;
        _SERR_READ_BYTES_OR_RETURN(read, &count, sizeof(count), true);

        void* slice = NULL;
        if (obj != NULL) {
            slice = snz_arenaPush(read->outArena, count * structSpec->size, 1);
            *(void**)((char*)obj + field->offsetInStruct) = slice;
            *(int64_t*)((char*)obj + field->type->offsetOfSliceLengthIntoStruct) = count; // FIXME: again, ensure length field is the correct size/type
        }

        for (int i = 0; i < count; i++) {
            void* offsetPos = slice;
            if (offsetPos != NULL) {
                offsetPos = (char*)slice + (i * structSpec->size);
            }
            for (_ser_SpecField* innerField = structSpec->firstField; innerField; innerField = innerField->next) {
                ser_ReadError err = _serr_readField(read, innerField, offsetPos);
                if (err != SER_RE_OK) {
                    return err;
                }
            }
        } // end slice loop
        return SER_RE_OK;
    } else if (kind == SER_TK_STRUCT) {
        _ser_SpecStruct* structSpec = field->type->referencedStruct;
        if (outPos && structSpec->pointable) {
            _ser_ptrTranslationSet(&read->ptrTable, read->positionIntoFile, (uint64_t)outPos);
        }
        for (_ser_SpecField* innerField = structSpec->firstField; innerField; innerField = innerField->next) {
            ser_ReadError err = _serr_readField(read, innerField, outPos);
            if (err != SER_RE_OK) {
                return err;
            }
        }
        return SER_RE_OK;
    } else if (kind == SER_TK_CSTRING) {
        uint64_t length = 0;
        _SERR_READ_BYTES_OR_RETURN(read, &length, sizeof(uint64_t), true);
        char* chars = NULL;
        if (length) {
            chars = SNZ_ARENA_PUSH_ARR(read->outArena, length + 1, char);
            _SERR_READ_BYTES_OR_RETURN(read, chars, length, false);
        }

        if (outPos) {
            *(char**)(outPos) = chars;
        }
        return SER_RE_OK;
    }

    SNZ_ASSERT(kind > SER_TK_INVALID && kind < SER_TK_COUNT, "invalid kind?? after validation tho??");
    int size = _ser_tKindSizes[kind];
    SNZ_ASSERTF(size != 0, "Kind of %d had no associated size.", kind);

    _SERR_READ_BYTES_OR_RETURN(read, outPos, size, true);
    return SER_RE_OK;
}

// FIXME: typeof macro instead of doing a separate type param?
#define ser_read(f, T, arena, scratch, outObj) _ser_read(f, #T, arena, scratch, outObj)
ser_ReadError _ser_read(FILE* file, const char* typename, snz_Arena* outArena, snz_Arena* scratch, void** outObj) {
    _ser_assertInstanceValidated();
    SNZ_ASSERT(outObj, "Expected a location to put result object.");

    _serr_ReadInst read = (_serr_ReadInst){
        .file = file,
        .positionIntoFile = 0,
        .outArena = outArena,
        .scratch = scratch,
    };

    _ser_SpecStruct* firstStructSpec = NULL;
    _ser_SpecStruct* lastStructSpec = NULL;
    _ser_SpecStruct* structSpecs = NULL;
    int64_t structSpecCount = 0;
    { // parse spec
        uint64_t version = 0;
        _SERR_READ_BYTES_OR_RETURN(&read, &version, sizeof(version), true);

        _SERR_READ_BYTES_OR_RETURN(&read, &structSpecCount, sizeof(structSpecCount), true);
        structSpecs = SNZ_ARENA_PUSH_ARR(scratch, structSpecCount, _ser_SpecStruct);

        for (int64_t i = 0; i < structSpecCount; i++) {
            _ser_SpecStruct* decl = &structSpecs[i];
            *decl = (_ser_SpecStruct){
                .indexIntoSpec = i,
            };
            if (lastStructSpec) {
                lastStructSpec->next = decl;
            } else {
                firstStructSpec = decl;
            }
            lastStructSpec = decl;

            int64_t tagLen = 0;
            _SERR_READ_BYTES_OR_RETURN(&read, &tagLen, sizeof(tagLen), true);
            char* tag = SNZ_ARENA_PUSH_ARR(read.scratch, tagLen + 1, char);
            _SERR_READ_BYTES_OR_RETURN(&read, tag, tagLen, false);
            decl->tag = tag;

            _SERR_READ_BYTES_OR_RETURN(&read, &decl->fieldCount, sizeof(decl->fieldCount), true);
            for (int64_t j = 0; j < decl->fieldCount; j++) {
                _ser_SpecField* field = SNZ_ARENA_PUSH(scratch, _ser_SpecField);
                if (decl->lastField) {
                    decl->lastField->next = field;
                } else {
                    decl->firstField = field;
                }
                decl->lastField = field;

                int64_t fieldTagLen = 0;
                _SERR_READ_BYTES_OR_RETURN(&read, &fieldTagLen, sizeof(fieldTagLen), true);
                tag = SNZ_ARENA_PUSH_ARR(read.scratch, fieldTagLen + 1, char);
                _SERR_READ_BYTES_OR_RETURN(&read, tag, fieldTagLen, false);
                field->tag = tag;

                field->type = SNZ_ARENA_PUSH(scratch, _ser_T);
                char fileT = 0;
                _SERR_READ_BYTES_OR_RETURN(&read, &fileT, 1, false);
                field->type->kind = (ser_TKind)fileT;

                if (field->type->kind == SER_TK_STRUCT) {
                    _SERR_READ_BYTES_OR_RETURN(&read, &field->type->referencedIndex, sizeof(field->type->referencedIndex), true);
                } else if ((field->type->kind == SER_TK_PTR) || (field->type->kind == SER_TK_SLICE)) {
                    _ser_T* structT = SNZ_ARENA_PUSH(scratch, _ser_T);
                    field->type->inner = structT;

                    fileT = 0;
                    _SERR_READ_BYTES_OR_RETURN(&read, &fileT, 1, false);
                    structT->kind = (ser_TKind)fileT;
                    SNZ_ASSERT(structT->kind == SER_TK_STRUCT, "HUH"); // FIXME: this is pre validation
                    _SERR_READ_BYTES_OR_RETURN(&read, &structT->referencedIndex, sizeof(structT->referencedIndex), true);
                }
            } // end field loop
        } // end decl loop
    } // end spec parsing

    { // compare to original & fix up
        for (_ser_SpecStruct* s = firstStructSpec; s; s = s->next) {
            _ser_SpecStruct* ogStruct = _ser_getStructSpecByName(s->tag);
            if (!ogStruct) {
                continue;
            }
            s->pointable = ogStruct->pointable;
            s->size = ogStruct->size;

            for (_ser_SpecField* f = s->firstField; f; f = f->next) {
                _ser_SpecField* ogField = _ser_getFieldSpecByName(ogStruct, f->tag);
                if (!ogField) {
                    f->missingFromStruct = true;
                    continue;
                }
                if (f->type->kind != ogField->type->kind) {
                    SNZ_LOGF("Kind mismatch in spec. For struct %s and field %s, current spec has kind %d, file has kind %d.",
                        s->tag, f->tag, ogField->type->kind, f->type->kind);
                    return SER_RE_UNRECOVERABLE_SPEC_MISMATCH;
                }
                f->offsetInStruct = ogField->offsetInStruct;

                if (f->type->kind == SER_TK_SLICE) {
                    // FIXME: the structure and kines of things haven't been validated here, that's kinda sloppy
                    f->type->offsetOfSliceLengthIntoStruct = ogField->type->offsetOfSliceLengthIntoStruct;
                }
                // FIXME: casting patches?
                // FIXME: post read patches?
            }
        }
        _ser_validate(firstStructSpec);
    }

    void* firstObj = NULL;
    _ser_SpecStruct* firstObjSpec = NULL;
    { // parse objs while any left
        while (true) { // FIXME: cutoff?
            int64_t kind = 0;
            _serr_readBytes(&read, &kind, sizeof(kind), true);
            if (feof(read.file)) { // here instead of the loop because this only triggers when you read over the bounds of the file
                break;
            }
            if (kind >= structSpecCount) {
                return SER_RE_GARBAGE_SPEC;
            }

            _ser_SpecStruct* spec = &structSpecs[kind];
            void* obj = snz_arenaPush(outArena, spec->size, 1);

            if (spec->pointable) {
                _ser_ptrTranslationSet(&read.ptrTable, read.positionIntoFile, (uint64_t)obj);
            }

            for (_ser_SpecField* field = spec->firstField; field; field = field->next) {
                ser_ReadError err = _serr_readField(&read, field, obj);
                if (err != SER_RE_OK) {
                    return err;
                }
            }

            if (!firstObj) {
                firstObj = obj;
                firstObjSpec = spec;
            }
        }
    } // end obj parse

    { // patch ptrs
        // FIXME: lack of typechecking of ptrs, VERY NOT GOOD PLEASE FIX
        // ^ please do a thorough look into whether this is a problem on write too
        // but we are kinda assuming clean input data for writes so idk (cause how are we sanitizing ptrs, especially from arenas)
        for (int i = 0; i < read.ptrTable.stubs.count; i++) {
            uint64_t locToPatch = read.ptrTable.stubs.locationsToPatch[i];
            uint64_t keyToPatchWith = read.ptrTable.stubs.keyOfValue[i];
            uint64_t* got = _ser_ptrTranslationGet(&read.ptrTable, keyToPatchWith);
            if (!got) {
                // FIXME: better err msg with types included
                SNZ_LOG("Broken pointer reference.");
                return SER_RE_PTR_PROBLEM;
            }
            *((uint64_t*)(locToPatch)) = *got;
        }
    }

    if (strcmp(firstObjSpec->tag, typename) != 0) {
        SNZ_LOGF("Wrong type of struct in file. Read expected '%s', file had '%s'.", typename, firstObjSpec->tag);
        return SER_RE_WRONG_TYPE_OF_STRUCT;
    }

    *outObj = firstObj;
    return SER_RE_OK;
}

#include "timeline.h"
#include "geometry.h"

typedef struct {
    const char* strA;
    const char* strB;
    const char* strC;
} _ser_TestSomeString;

void ser_tests() {
    snz_testPrintSection("ser 3");
    snz_Arena testArena = snz_arenaInit(10000000, "test arena");

    ser_begin(&testArena);

    ser_addStruct(HMM_Vec2, false);
    ser_addStructField(HMM_Vec2, ser_tBase(SER_TK_FLOAT32), X);
    ser_addStructField(HMM_Vec2, ser_tBase(SER_TK_FLOAT32), Y);

    ser_addStruct(HMM_Vec3, false);
    ser_addStructField(HMM_Vec3, ser_tBase(SER_TK_FLOAT32), X);
    ser_addStructField(HMM_Vec3, ser_tBase(SER_TK_FLOAT32), Y);
    ser_addStructField(HMM_Vec3, ser_tBase(SER_TK_FLOAT32), Z);

    ser_addStruct(geo_Tri, false);
    ser_addStructField(geo_Tri, ser_tStruct(HMM_Vec3), a);
    ser_addStructField(geo_Tri, ser_tStruct(HMM_Vec3), b);
    ser_addStructField(geo_Tri, ser_tStruct(HMM_Vec3), c);

    ser_addStruct(geo_TriSlice, false);
    ser_addStructFieldSlice(geo_TriSlice, geo_Tri, elems, count);

    // ser_addStruct(tl_Op, true);
    // ser_addStructField(tl_Op, ser_tPtr(tl_Op), next);
    // ser_addStructField(tl_Op, ser_tBase(SER_TK_INT32), kind);
    // ser_addStructField(tl_Op, ser_tPtr(tl_Op), dependencies[0]);
    // ser_addStructField(tl_Op, ser_tStruct(HMM_Vec2), ui.pos);

    ser_addStruct(_ser_TestSomeString, false);
    ser_addStructField(_ser_TestSomeString, ser_tBase(SER_TK_CSTRING), strA);
    ser_addStructField(_ser_TestSomeString, ser_tBase(SER_TK_CSTRING), strB);
    ser_addStructField(_ser_TestSomeString, ser_tBase(SER_TK_CSTRING), strC);

    ser_end();

    {
        FILE* f = fopen("testing/ser3.adder", "wb");
        geo_Tri tris[] = {
            (geo_Tri) {
                .a = HMM_V3(0, 1, 2),
                .b = HMM_V3(3, 4, 5),
                .c = HMM_V3(6, 7, 8),
            },
            (geo_Tri) {
                .a = HMM_V3(0, 10, 20),
                .b = HMM_V3(30, 40, 50),
                .c = HMM_V3(60, 70, 80),
            },
        };
        geo_TriSlice slice = (geo_TriSlice){
            .elems = tris,
            .count = sizeof(tris) / sizeof(*tris),
        };
        SNZ_ASSERT(ser_write(f, geo_TriSlice, &slice, &testArena) == SER_WE_OK, "write failure");

        // tl_Op ops[] = {
        //     (tl_Op) {
        //         .kind = TL_OPK_BASE_GEOMETRY,
        //         .ui.pos = HMM_V2(10, 10),
        //     },
        //     (tl_Op) {
        //         .kind = TL_OPK_BASE_GEOMETRY,
        //         .ui.pos = HMM_V2(1, 1),
        //     },
        //     (tl_Op) {
        //         .kind = TL_OPK_SKETCH,
        //         .ui.pos = HMM_V2(2, 2),
        //     },
        // };
        // ops[0].next = &ops[1];
        // ops[1].next = &ops[2];
        // ops[1].dependencies[0] = &ops[2];

        // ser_write(f, tl_Op, &ops[0], &testArena);
        fclose(f);

        f = fopen("testing/ser3.adder", "rb");
        geo_TriSlice* obj = NULL;
        ser_ReadError err = ser_read(f, geo_TriSlice, &testArena, &testArena, (void**)&obj);
        SNZ_ASSERTF(err == SER_RE_OK, "Read failed, code: %d.", err);
        fclose(f);
    }

    {
        FILE* f = fopen("testing/serStrTest.adder", "wb");
        _ser_TestSomeString strs = (_ser_TestSomeString){
            .strA = "THIS IS THE FIRST ONE!!",
            .strB = "THIS IS THE SECOND ONE!!",
            .strC = "LAST ONE HERE!!!!",
        };
        ser_WriteError writeErr = ser_write(f, _ser_TestSomeString, &strs, &testArena);
        SNZ_ASSERTF(writeErr == SER_WE_OK, "Write failed, code: %d.", writeErr);
        fclose(f);

        _ser_TestSomeString* outStrs = NULL;
        f = fopen("testing/serStrTest.adder", "rb");
        ser_ReadError err = ser_read(f, _ser_TestSomeString, &testArena, &testArena, (void**)&outStrs);
        SNZ_ASSERTF(err == SER_RE_OK, "Read failed, code: %d.", err);
        fclose(f);

        SNZ_ASSERTF(strcmp(strs.strA, outStrs->strA) == 0,
            "String A differed before and after write to file.\nBefore: %s,\nAfter: %s",
            strs.strA, outStrs->strA);
        SNZ_ASSERTF(strcmp(strs.strB, outStrs->strB) == 0,
            "String B differed before and after write to file.\nBefore: %s,\nAfter: %s",
            strs.strB, outStrs->strB);
        SNZ_ASSERTF(strcmp(strs.strC, outStrs->strC) == 0,
            "String B differed before and after write to file.\nBefore: %s,\nAfter: %s",
            strs.strC, outStrs->strC);

        snz_testPrint(true, "strings to and from file");
    }

    snz_arenaDeinit(&testArena);

    // reset globals so everything else works right
    memset(&_ser_globs, 0, sizeof(_ser_globs));
}

// FIXME: a fuzzing system for this lib is 100000% possible, do that please
