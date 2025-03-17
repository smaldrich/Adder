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
    int offsetOfSliceLengthIntoStruct;
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
    int64_t indexIntoSpec;
    bool pointable;

    ser_SpecField* firstField;
    int64_t fieldCount;
    int64_t size;
};

static struct {
    bool validated; // when true indicates all specs have been added and that no more will, also that the entire thing has been validated.
    ser_SpecStruct* firstStructSpec;
    int64_t structSpecCount;

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
    SNZ_ASSERT(kind != SER_TK_SLICE, "Kind of 3 (SER_TK_SLICE) isn't a base kind.");
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
    SNZ_ASSERT(innerT->kind == SER_TK_STRUCT, "Can't point to a non-struct type.");
    out->inner = innerT;
    return out;
}

#define ser_addStruct(T, pointable) _ser_addStruct(#T, sizeof(T), pointable)
void _ser_addStruct(const char* name, int64_t size, bool pointable) {
    _ser_assertInstanceValidForAddingToSpec();
    _ser_pushActiveStructSpecIfAny();
    ser_SpecStruct* s = SNZ_ARENA_PUSH(_ser_globs.specArena, ser_SpecStruct);
    _ser_globs.activeStructSpec = s;
    s->tag = name;
    s->size = size;
    s->pointable = pointable;
}

// FIXME: assert that active struct name == struct name?
// FIXME: assert that size of kind is same as sizeof prop named
#define ser_addStructField(type, structT, name) \
    _ser_addStructField(type, #name, offsetof(structT, name))
void _ser_addStructField(ser_T* type, const char* tag, int offsetIntoStruct) {
    _ser_assertInstanceValidForAddingToSpec();
    SNZ_ASSERT(_ser_globs.activeStructSpec, "There was no active struct to add a field to.");

    ser_SpecField* field = SNZ_ARENA_PUSH(_ser_globs.specArena, ser_SpecField);
    *field = (ser_SpecField){
        .offsetInStruct = offsetIntoStruct,
        .tag = tag,
        .type = type,
        .next = _ser_globs.activeStructSpec->firstField,
    };
    _ser_globs.activeStructSpec->firstField = field;
    _ser_globs.activeStructSpec->fieldCount++;
}

// FIXME: assert that prop for pength is an int64
#define ser_addStructFieldSlice(innerT, structT, ptrName, lengthName) _ser_addStructFieldSlice(#innerT, #ptrName, offsetof(structT, ptrName), offsetof(structT, lengthName))
void _ser_addStructFieldSlice(const char* innerTypeName, const char* tag, int ptrOffsetIntoStruct, int lengthOffsetIntoStruct) {
    ser_T* type = SNZ_ARENA_PUSH(_ser_globs.specArena, ser_T);
    *type = (ser_T){
        .inner = _ser_tStruct(innerTypeName),
        .kind = SER_TK_SLICE,
        .offsetOfSliceLengthIntoStruct = lengthOffsetIntoStruct,
    };
    _ser_addStructField(type, tag, ptrOffsetIntoStruct);
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
                } else if (inner->kind == SER_TK_SLICE) {
                    SNZ_ASSERT(inner->inner, "slice with no inner kind");
                    SNZ_ASSERT(inner->inner->kind == SER_TK_STRUCT, "slice that doesn't point to struct type");
                } else {
                    SNZ_ASSERT(!inner->inner, "terminal kind with an inner kind");
                }
            } // end validating inners on field

            for (ser_T* inner = f->type; inner; inner = inner->inner) {
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
    _ser_globs.validated = true;
}

typedef struct _ser_AddressNode _ser_AddressNode;
struct _ser_AddressNode {
    uint64_t key;
    uint64_t value;
    _ser_AddressNode* nextCollided;
};

typedef struct _serw_QueuedStruct _serw_QueuedStruct;
struct _serw_QueuedStruct {
    _serw_QueuedStruct* next;
    ser_SpecStruct* spec;
    void* obj;
};

// FIXME: dyn size for large files
#define _SERW_ADDRESS_BUCKET_COUNT 2048
#define _SERW_ADDRESS_NODE_COUNT 2048
#define _SERW_PTR_STUB_COUNT 2048

typedef struct {
    _serw_QueuedStruct* nextStruct;
    _ser_AddressNode* addressBuckets[_SERW_ADDRESS_BUCKET_COUNT];
    _ser_AddressNode addresses[_SERW_ADDRESS_NODE_COUNT];
    int64_t addressCount;

    struct {
        // FIXME: dyn sizing
        uint64_t locations[_SERW_PTR_STUB_COUNT];
        uint64_t targetAddresses[_SERW_PTR_STUB_COUNT];
        int64_t count;
    } stubs;

    FILE* file;
    uint64_t positionIntoFile;
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

bool _ser_isSystemLittleEndian() {
    uint64_t x = 1;
    return ((char*)&x)[0] == 1;
}

void _serw_writeBytes(_serw_WriteInst* write, const void* src, int64_t size, bool swapWithEndianness) {
    uint8_t* srcChars = (uint8_t*)src;
    if (swapWithEndianness && _ser_isSystemLittleEndian()) {
        uint8_t* buf = snz_arenaPush(write->scratch, size);
        for (int64_t i = 0; i < size; i++) {
            buf[i] = srcChars[size - 1 - i];
        }
        write->positionIntoFile += size;
        fwrite(buf, size, 1, write->file);
        snz_arenaPop(write->scratch, size);
    } else {
        write->positionIntoFile += size;
        fwrite(src, size, 1, write->file);
    }

    // int count = (8 - (write->positionIntoFile % 8)) % 8;
    // write->positionIntoFile += count;
    // for (int i = 0; i < count; i++) {
    //     uint8_t byte = 0xFF;
    //     fwrite(&byte, 1, 1, write->file);
    // }
}

void _serw_writeField(_serw_WriteInst* write, ser_SpecField* field, void* obj) {
    ser_TKind kind = field->type->kind;
    if (kind == SER_TK_PTR) {
        uint64_t pointed = (uint64_t) * (void**)((char*)obj + field->offsetInStruct);
        if (pointed != 0 && !_serw_addressLocGet(write, pointed)) {
            _serw_QueuedStruct* queued = SNZ_ARENA_PUSH(write->scratch, _serw_QueuedStruct);
            *queued = (_serw_QueuedStruct){
                .next = write->nextStruct,
                .obj = (void*)pointed,
                .spec = field->type->inner->referencedStruct,
            };
            write->nextStruct = queued;
            SNZ_ASSERT(_serw_addressLocSet(write, pointed, 0), "huh");
        } // FIXME: just put the location in file here if the object has already been written

        if (pointed != 0) {
            SNZ_ASSERTF(write->stubs.count < _SERW_PTR_STUB_COUNT, "Too many ptr stubs: %d", write->stubs.count);
            write->stubs.locations[write->stubs.count] = write->positionIntoFile;
            write->stubs.targetAddresses[write->stubs.count] = pointed;
            write->stubs.count++;
        }
        _serw_writeBytes(write, &pointed, sizeof(void*), true); // this is overwritten when the ptr is patched (if non-null)
        return;
    } else if (kind == SER_TK_SLICE) {
        uint64_t pointed = (uint64_t) * (void**)((char*)obj + field->offsetInStruct);
        int64_t length = *(int64_t*)((char*)obj + field->type->offsetOfSliceLengthIntoStruct);
        _serw_writeBytes(write, &length, sizeof(length), true);
        ser_SpecStruct* innerStructType = field->type->inner->referencedStruct;
        for (int64_t i = 0; i < length; i++) {
            uint64_t innerStructAddress = pointed + (i * innerStructType->size);
            for (ser_SpecField* innerField = innerStructType->firstField; innerField; innerField = innerField->next) {
                _serw_writeField(write, innerField, (void*)innerStructAddress);
            }
        }
        return;
    } else if (kind == SER_TK_STRUCT) {
        ser_SpecStruct* s = field->type->referencedStruct;
        void* innerStruct = (char*)obj + field->offsetInStruct;

        if (s->pointable) {
            _serw_addressLocSet(write, (uint64_t)innerStruct, write->positionIntoFile);
        }
        for (ser_SpecField* innerField = s->firstField; innerField; innerField = innerField->next) {
            _serw_writeField(write, innerField, innerStruct);
        }
        return;
    }


    void* ptr = ((char*)obj) + field->offsetInStruct;
    int sizes[SER_TK_COUNT] = { 0 };
    sizes[SER_TK_FLOAT32] = sizeof(float);
    sizes[SER_TK_FLOAT64] = sizeof(double);
    sizes[SER_TK_INT8] = sizeof(int8_t);
    sizes[SER_TK_INT16] = sizeof(int16_t);
    sizes[SER_TK_INT32] = sizeof(int32_t);
    sizes[SER_TK_INT64] = sizeof(int64_t);
    sizes[SER_TK_UINT8] = sizeof(uint8_t);
    sizes[SER_TK_UINT16] = sizeof(uint16_t);
    sizes[SER_TK_UINT32] = sizeof(uint32_t);
    sizes[SER_TK_UINT64] = sizeof(uint64_t);

    int size = sizes[kind];
    SNZ_ASSERTF(size != 0, "Kind of %d had no associated size.", kind);
    _serw_writeBytes(write, ptr, size, true);
}

// FIXME: typecheck of some kind on obj
#define ser_write(F, T, obj, scratch) _ser_write(F, #T, obj, scratch)
void _ser_write(FILE* f, const char* typename, void* seedObj, snz_Arena* scratch) {
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
        _serw_writeBytes(&write, &version, sizeof(version), true);

        _serw_writeBytes(&write, &_ser_globs.structSpecCount, sizeof(_ser_globs.structSpecCount), true); // decl count
        for (ser_SpecStruct* s = _ser_globs.firstStructSpec; s; s = s->next) {
            uint64_t tagLen = strlen(s->tag);
            _serw_writeBytes(&write, &tagLen, sizeof(tagLen), true); // length of tag
            _serw_writeBytes(&write, s->tag, tagLen, false); // tag

            _serw_writeBytes(&write, &s->fieldCount, sizeof(s->fieldCount), true); // field count
            for (ser_SpecField* field = s->firstField; field; field = field->next) {
                uint64_t tagLen = strlen(field->tag);
                _serw_writeBytes(&write, &tagLen, sizeof(tagLen), true); // length of tag
                _serw_writeBytes(&write, field->tag, tagLen, false); // tag
                for (ser_T* inner = field->type; inner; inner = inner->inner) {
                    uint8_t enumVal = (uint8_t)inner->kind;
                    _serw_writeBytes(&write, &enumVal, 1, false);
                    if (inner->kind == SER_TK_STRUCT) {
                        int64_t idx = inner->referencedStruct->indexIntoSpec;
                        _serw_writeBytes(&write, &idx, sizeof(idx), true);
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
        _serw_writeBytes(&write, &structIdx, sizeof(structIdx), true); // kind of structs

        if (s->spec->pointable) {
            uint64_t* got = _serw_addressLocGet(&write, (uint64_t)s->obj);
            if (got) {
                SNZ_ASSERT(*got == (uint64_t)0, "AHHHHHH");
            } // assert not already written to file
            _serw_addressLocSet(&write, (uint64_t)s->obj, write.positionIntoFile);
        }
        for (ser_SpecField* field = s->spec->firstField; field; field = field->next) {
            _serw_writeField(&write, field, s->obj);
        }
    }

    { // patch ptrs
        for (int64_t i = 0; i < write.stubs.count;i++) {
            int64_t fileLoc = write.stubs.locations[i];
            fseek(write.file, fileLoc, 0);

            // FIXME: do we really need the stubs array? can't you just loop thru the address translations,
            // assert they have all been hit, and then patch?
            uint64_t* otherLoc = _serw_addressLocGet(&write, write.stubs.targetAddresses[i]);
            SNZ_ASSERT(otherLoc, "unmatched ptr!!!");
            _serw_writeBytes(&write, otherLoc, sizeof(*otherLoc), true);
        }
    }
}

#include "timeline.h"
#include "geometry.h"

void ser_tests() {
    snz_testPrintSection("ser 3");
    snz_Arena testArena = snz_arenaInit(10000000, "test arena");

    ser_begin(&testArena);

    ser_addStruct(HMM_Vec2, false);
    ser_addStructField(ser_tBase(SER_TK_FLOAT32), HMM_Vec2, X);
    ser_addStructField(ser_tBase(SER_TK_FLOAT32), HMM_Vec2, Y);

    ser_addStruct(HMM_Vec3, false);
    ser_addStructField(ser_tBase(SER_TK_FLOAT32), HMM_Vec3, X);
    ser_addStructField(ser_tBase(SER_TK_FLOAT32), HMM_Vec3, Y);
    ser_addStructField(ser_tBase(SER_TK_FLOAT32), HMM_Vec3, Z);

    // ser_addStruct(geo_Tri, false);
    // ser_addStructField(ser_tStruct(HMM_Vec3), geo_Tri, a);
    // ser_addStructField(ser_tStruct(HMM_Vec3), geo_Tri, b);
    // ser_addStructField(ser_tStruct(HMM_Vec3), geo_Tri, c);

    // ser_addStruct(geo_TriSlice, false);
    // ser_addStructFieldSlice(geo_Tri, geo_TriSlice, elems, count);

    // ser_addStruct(set_Settings);
    // ser_addStructField(ser_tBase(SER_TK_INT8), set_Settings, darkMode);
    // ser_addStructField(ser_tBase(SER_TK_INT8), set_Settings, musicMode);
    // ser_addStructField(ser_tBase(SER_TK_INT8), set_Settings, skybox);
    // ser_addStructField(ser_tBase(SER_TK_INT8), set_Settings, hintWindowAlwaysOpen);
    // ser_addStructField(ser_tBase(SER_TK_INT8), set_Settings, leftBarAlwaysOpen);
    // ser_addStructField(ser_tBase(SER_TK_INT8), set_Settings, timelinePreviewSpinBackground);
    // ser_addStructField(ser_tBase(SER_TK_INT8), set_Settings, squishyCamera);
    // ser_addStructField(ser_tBase(SER_TK_INT8), set_Settings, crosshair);

    ser_addStruct(tl_Op, true);
    ser_addStructField(ser_tPtr(ser_tStruct(tl_Op)), tl_Op, next);
    ser_addStructField(ser_tBase(SER_TK_INT32), tl_Op, kind);
    ser_addStructField(ser_tPtr(ser_tStruct(tl_Op)), tl_Op, dependencies[0]);
    ser_addStructField(ser_tStruct(HMM_Vec2), tl_Op, ui.pos);

    ser_end();

    FILE* f = fopen("testing/ser3.adder", "w");
    // geo_Tri tris[] = {
    //     (geo_Tri) {
    //         .a = HMM_V3(0, 1, 2),
    //         .b = HMM_V3(3, 4, 5),
    //         .c = HMM_V3(6, 7, 8),
    //     },
    //     (geo_Tri) {
    //         .a = HMM_V3(0, 10, 20),
    //         .b = HMM_V3(30, 40, 50),
    //         .c = HMM_V3(60, 70, 80),
    //     },
    // };
    // geo_TriSlice slice = (geo_TriSlice){
    //     .elems = tris,
    //     .count = sizeof(tris) / sizeof(*tris),
    // };
    // ser_write(f, geo_TriSlice, &slice, &testArena);

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
