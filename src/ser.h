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

typedef enum {
    SER_TK_INVALID,

    SER_TK_STRUCT,
    SER_TK_PTR,
    SER_TK_PTR_VIEW,

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
    "SER_TK_PTR_VIEW",

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
    int offsetOfPtrViewLengthIntoStruct;
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
    int64_t size;
};

static struct {
    bool validated; // when true indicates all specs have been added and that no more will, also that the entire thing has been validated.
    ser_SpecStruct* firstStructSpec;
    int structSpecCount;

    snz_Arena* specArena;
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
    SNZ_ASSERT(kind != SER_TK_PTR_VIEW, "Kind of 3 (SER_TK_PTR_VIEW) isn't a base kind.");
    ser_T* out = SNZ_ARENA_PUSH(_ser_globs.specArena, ser_T);
    out->kind = kind;
    return out;
}

#define ser_tStruct(T) _ser_tStruct(#T)
ser_T* _ser_tStruct(const char* name) {
    _ser_assertInstanceValidForAddingToSpec();
    ser_T* out = SNZ_ARENA_PUSH(_ser_globs.specArena, ser_T);
    out->kind = SER_TK_STRUCT;
    out->referencedName = name;
    return out;
}

// FIXME: struct types only in here plz
ser_T* ser_tPtr(ser_T* innerT) {
    _ser_assertInstanceValidForAddingToSpec();
    ser_T* out = SNZ_ARENA_PUSH(_ser_globs.specArena, ser_T);
    out->kind = SER_TK_PTR;
    SNZ_ASSERT(innerT, "Can't point to a null type.");
    out->inner = innerT;
    return out;
}

// FIXME: pointable flag
#define ser_addStruct(T) _ser_addStruct(#T, sizeof(T))
void _ser_addStruct(const char* name, int64_t size) {
    _ser_assertInstanceValidForAddingToSpec();
    _ser_pushActiveStructSpecIfAny();
    ser_SpecStruct* s = SNZ_ARENA_PUSH(_ser_globs.specArena, ser_SpecStruct);
    _ser_globs.activeStructSpec = s;
    s->tag = name;
    s->size = size;
}

// FIXME: assert that active struct name == struct name?
// FIXME: assert that size of kind is same as sizeof prop named
#define ser_addStructField(kindPtr, structT, name) \
    _ser_addStructField(kindPtr, #name, offsetof(structT, name))
void _ser_addStructField(ser_T* kind, const char* tag, int offsetIntoStruct) {
    _ser_assertInstanceValidForAddingToSpec();
    SNZ_ASSERT(_ser_globs.activeStructSpec, "There was no active struct to add a field to.");

    ser_SpecField* field = SNZ_ARENA_PUSH(_ser_globs.specArena, ser_SpecField);
    *field = (ser_SpecField){
        .offsetInStruct = offsetIntoStruct,
        .tag = tag,
        .type = kind,
        .next = _ser_globs.activeStructSpec->firstField,
    };
    _ser_globs.activeStructSpec->firstField = field;
    _ser_globs.activeStructSpec->fieldCount++;
}

// FIXME: switch on type and or assert that len prop is an int64_t
#define ser_addStructFieldPtrView(innerKind, structT, ptrPropName, lenPropName) \
    _ser_addStructFieldPtrView(innerKind, #ptrPropName, offsetof(structT, ptrPropName), offsetof(structT, lenPropName))
void _ser_addStructFieldPtrView(ser_T* innerKind, const char* ptrPropTag, int ptrPropOffset, int lenPropOffset) {
    _ser_assertInstanceValidForAddingToSpec();

    SNZ_ASSERT(innerKind->kind != SER_TK_PTR_VIEW, "can't nest ptr views cause the inner length woudn't exist anywhere. Wrap it in a struct.");
    ser_T* viewT = SNZ_ARENA_PUSH(_ser_globs.specArena, ser_T);
    *viewT = (ser_T){
        .inner = innerKind,
        .kind = SER_TK_PTR_VIEW,
        .offsetOfPtrViewLengthIntoStruct = lenPropOffset,
    };

    _ser_addStructField(viewT, ptrPropTag, ptrPropOffset);
}

void ser_begin(snz_Arena* specArena) {
    _ser_globs.specArena = specArena;
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
    if (kind == SER_TK_PTR) {
        return true;
    } else if (kind == SER_TK_PTR_VIEW) {
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
            SNZ_ASSERT(f->type, "can't have a field with no type.");

            for (ser_T* inner = f->type; inner; inner = inner->inner) {
                SNZ_ASSERTF(inner->kind > SER_TK_INVALID && inner->kind < SER_TK_COUNT, "invalid kind: %d.", inner->kind);
                if (inner->kind == SER_TK_STRUCT) {
                    inner->referencedStruct = _ser_getStructSpecByName(inner->referencedName);
                    SNZ_ASSERTF(inner->referencedStruct, "no definition with the name '%s' found.", inner->referencedName);

                    if (inner == f->type) {
                        SNZ_ASSERT(inner->referencedStruct != s, "can't nest structs. make the field a ptr or ptr view.");
                    }
                } else if (inner->kind == SER_TK_PTR) {
                    SNZ_ASSERT(inner->inner, "ptr with no inner kind");
                    SNZ_ASSERT(inner->inner->kind == SER_TK_STRUCT, "ptr that doesn't point to struct type");
                } else if (inner->kind == SER_TK_PTR_VIEW) {
                    SNZ_ASSERT(inner->inner, "ptr view with no inner kind");
                    SNZ_ASSERT(inner->inner->kind == SER_TK_STRUCT, "ptr view that doesn't point to struct type");
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

typedef struct _ser_AddressNode _ser_AddressNode;
struct _ser_AddressNode {
    uint64_t key;
    uint64_t value;
    _ser_AddressNode* nextCollided;
};

typedef struct _serw_QueuedStructs _serw_QueuedStructs;
struct _serw_QueuedStructs {
    _serw_QueuedStructs* next;
    ser_SpecStruct* spec;
    void* obj;
    int64_t count;
};

// FIXME: dyn size for large files
#define _SERW_ADDRESS_BUCKET_COUNT 2048
#define _SERW_ADDRESS_NODE_COUNT 2048

typedef struct {
    _serw_QueuedStructs* nextStruct;
    _ser_AddressNode* addressBuckets[_SERW_ADDRESS_BUCKET_COUNT];
    _ser_AddressNode addresses[_SERW_ADDRESS_NODE_COUNT];
    int64_t addressCount;
    // locations of loc stubs in file
    FILE* file;
    snz_Arena* scratch;
} _serw_WriteInst;

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
bool _serw_addressLocSet(_serw_WriteInst* write, uint64_t key, uint64_t value) {
    int64_t bucket = _ser_addressHash(key) % _SERW_ADDRESS_BUCKET_COUNT;
    _ser_AddressNode* initial = write->addressBuckets[bucket];
    for (_ser_AddressNode* node = initial; node; node = node->nextCollided) {
        if (node->key == key) {
            node->value = value;
            return false;
        }
    }

    SNZ_ASSERT(write->addressCount < _SERW_ADDRESS_NODE_COUNT, "Too many addresses to serialize. see the fixme.");
    _ser_AddressNode* node = &write->addresses[write->addressCount];
    write->addressCount++;
    *node = (_ser_AddressNode){
        .key = key,
        .value = value,
        .nextCollided = initial,
    };
    write->addressBuckets[bucket] = node;
    return true;
}

// null if not in table
uint64_t* _serw_addressLocGet(_serw_WriteInst* write, uint64_t key) {
    int64_t bucket = _ser_addressHash(key) % _SERW_ADDRESS_BUCKET_COUNT;
    for (_ser_AddressNode* node = write->addressBuckets[bucket]; node; node = node->nextCollided) {
        if (node->key == key) {
            return &node->value;
        }
    }
    return NULL;
}

void _serw_writeField(_serw_WriteInst* write, ser_SpecField* field, void* obj, int indentLevel) {
    fprintf(write->file, "%*s", indentLevel * 4, "");

    ser_TKind kind = field->type->kind;
    if (kind == SER_TK_PTR || kind == SER_TK_PTR_VIEW) {
        uint64_t pointed = (uint64_t) * (void**)((char*)obj + field->offsetInStruct);
        int64_t queuedCount = 1;
        if (kind == SER_TK_PTR_VIEW) {
            queuedCount = *(int64_t*)((char*)obj + field->type->offsetOfPtrViewLengthIntoStruct);
            if (queuedCount == 0) {
                pointed = 0;
            }
        }

        if (pointed != 0 && !_serw_addressLocGet(write, pointed)) {
            _serw_QueuedStructs* queued = SNZ_ARENA_PUSH(write->scratch, _serw_QueuedStructs);
            *queued = (_serw_QueuedStructs){
                .count = queuedCount,
                .next = write->nextStruct,
                .obj = (void*)pointed,
                .spec = field->type->inner->referencedStruct,
            };
            write->nextStruct = queued;

            uint64_t structStart = pointed;
            for (int i = 0; i < queued->count; i++) {
                SNZ_ASSERT(_serw_addressLocSet(write, structStart, 0), "huh");
                structStart += queued->spec->size;
            }
        }

        // FIXME: put file loc to stub table
        fprintf(write->file, "0x%p", (void*)pointed);
        if (kind == SER_TK_PTR_VIEW) {
            fprintf(write->file, ", %lld elems", queuedCount);
        }
        fprintf(write->file, "\n");
        return;
    } else if (kind == SER_TK_STRUCT) {
        fprintf(write->file, ">> struct\n");
        ser_SpecStruct* s = field->type->referencedStruct;
        void* innerStruct = (char*)obj + field->offsetInStruct;
        for (ser_SpecField* innerField = s->firstField; innerField; innerField = innerField->next) {
            _serw_writeField(write, innerField, innerStruct, indentLevel + 1);
        }
        return;
    }


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
    } else {
        SNZ_ASSERTF(false, "unreachable. kind: %d.", kind);
    }
    fprintf(write->file, "\n");
}

// FIXME: typecheck of some kind on obj
#define ser_write(F, T, obj, scratch) _ser_write(F, #T, obj, scratch)
void _ser_write(FILE* f, const char* typename, void* seedObj, snz_Arena* scratch) {
    _ser_assertInstanceValidated();

    _serw_WriteInst write = { 0 };
    write.file = f;
    write.scratch = scratch;

    write.nextStruct = SNZ_ARENA_PUSH(scratch, _serw_QueuedStructs);
    write.nextStruct->obj = seedObj;
    write.nextStruct->count = 1;
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
        _serw_QueuedStructs* s = write.nextStruct;
        write.nextStruct = s->next;

        SNZ_ASSERTF(s->count >= 1, "queued struct with %lld count.", s->count);
        // FIXME: the extra bytes for count make me angry, systems with large amts of isolated (via ptr) structures are shit in this - which is most of them :)
        fprintf(write.file, "%lld structs, kind: %d\n", s->count, s->spec->indexIntoSpec);
        for (int i = 0; i < s->count; i++) {
            void* innerStructLoc = (char*)(s->obj) + (i * s->spec->size);
            for (ser_SpecField* field = s->spec->firstField; field; field = field->next) {
                _serw_writeField(&write, field, innerStructLoc, 1);
            }
        }
    }
}

#include "timeline.h"
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
    ser_addStructFieldPtrView(ser_tStruct(geo_Tri), geo_TriSlice, elems, count);

    ser_addStruct(set_Settings);
    ser_addStructField(ser_tBase(SER_TK_INT8), set_Settings, darkMode);
    ser_addStructField(ser_tBase(SER_TK_INT8), set_Settings, musicMode);
    ser_addStructField(ser_tBase(SER_TK_INT8), set_Settings, skybox);
    ser_addStructField(ser_tBase(SER_TK_INT8), set_Settings, hintWindowAlwaysOpen);
    ser_addStructField(ser_tBase(SER_TK_INT8), set_Settings, leftBarAlwaysOpen);
    ser_addStructField(ser_tBase(SER_TK_INT8), set_Settings, timelinePreviewSpinBackground);
    ser_addStructField(ser_tBase(SER_TK_INT8), set_Settings, squishyCamera);
    ser_addStructField(ser_tBase(SER_TK_INT8), set_Settings, crosshair);

    ser_addStruct(tl_Op);
    ser_addStructField(ser_tPtr(ser_tStruct(tl_Op)), tl_Op, next);
    ser_addStructField(ser_tBase(SER_TK_INT32), tl_Op, kind);
    ser_addStructField(ser_tPtr(ser_tStruct(tl_Op)), tl_Op, dependencies[0]);
    ser_addStructField(ser_tStruct(HMM_Vec2), tl_Op, ui.pos);

    ser_end();

    FILE* f = fopen("testing/ser3.adder", "w");
    tl_Op ops[] = {
        (tl_Op) {
            .kind = TL_OPK_BASE_GEOMETRY,
            .ui.pos = HMM_V2(0, 0),
        },
        (tl_Op) {
            .kind = TL_OPK_BASE_GEOMETRY,
            .ui.pos = HMM_V2(1, 1),
        },
        (tl_Op) {
            .kind = TL_OPK_SKETCH,
            .ui.pos = HMM_V2(2, 2),
        },
    };
    ops[0].next = &ops[1];
    ops[1].next = &ops[2];
    ops[1].dependencies[0] = &ops[2];

    ser_write(f, tl_Op, &ops[0], &testArena);
    fclose(f);
}