#pragma once
#include <inttypes.h>
#include <memory.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>

#include "base/allocators.h"

typedef enum {
    SERE_OK,
    SERE_FOPEN_FAILED,
    SERE_FWRITE_FAILED,
    SERE_FREAD_FAILED,
    SERE_INVALID_DECL_REF_TAG,
    SERE_INVALID_DECL_REF_ID,
    SERE_TEST_EXPECT_FAILED,
    SERE_SPECSET_EMPTY,
    SERE_GLOBAL_SET_UNLOCKED,
    SERE_GLOBAL_SET_LOCKED,
    SERE_SPECSET_INCORRECT_DECL_COUNT,
    SERE_SPECSET_INCORRECT_PROP_COUNT,
    SERE_DUPLICATE_DECL_TAGS,
    SERE_DUPLICATE_PROP_TAGS,
    SERE_EMPTY_ENUM,
    SERE_EMPTY_TAG,
    SERE_EMPTY_STRUCT_DECL,
    SERE_NO_OFFSETS,
    SERE_UNEXPECTED_KIND,
    SERE_PARSE_FAILED,
    SERE_VA_ARG_MISUSE,
    SERE_SPEC_MISMATCH,
    SERE_INVALID_PULL_TYPE,
    SERE_ARRAY_OF_ARRAYS,
    SERE_PTR_TO_ARRAY,
    SERE_TERMINAL_WITH_INNER,
    SERE_NONTERMINAL_WITH_NO_INNER,
    SERE_UNRESOLVED_PTR,
} ser_Error;
// TODO: array of readable strings for each error code

// TODO: organize error enum
// TODO: singleheader-ify this
// TODO: docs pass
// TODO: write down possible error returns from each public function
// TODO: warn on unused results

#define _SER_EXPECT(expr, error) \
    do { \
        ser_Error _e = expr; \
        if (!_e) { \
            /* printf("[ser expect]: %s:%d: failed\n", __FILE__, __LINE__); */ \
            return error; \
        } \
    } while (0)

// does not double eval expr :)
// returns the result of expr if expr evaluates to something other than SERE_OK
#define _SER_EXPECT_OK(expr)  \
    do {                            \
        ser_Error _e = expr;        \
        if (_e != SERE_OK) {        \
            /* printf("[ser expect ok]: %s:%d: failed with code %d\n", __FILE__, __LINE__, _e); */ \
            return _e;              \
        }                           \
    } while (0)

#define SER_ASSERT(expr) assert(expr)
// created on some public functions so that they fail unrecoverably in user code, but not in testing code
#define SER_ASSERT_OK(expr) (SER_ASSERT(expr == SERE_OK))

typedef enum {
    SER_DK_STRUCT,
    SER_DK_ENUM,
    SER_DK_COUNT,
} _ser_DeclKind;

typedef enum {
    SER_PK_CHAR,
    SER_PK_INT,
    SER_PK_FLOAT,
    // SER_PK_ARRAY_INTERNAL, // an array that fits completely within a struct // TODO: this
    SER_PK_ARRAY_EXTERNAL, // an array allocated outside of the struct
    SER_PK_PTR,
    SER_PK_DECL_REF, // indicates that the type for this is another declaration (struct or enum)
    SER_PK_COUNT,
} _ser_PropKind;
// ^^^^VVVV order between these needs to stay consistant for parser to work properly
// decl ref intentionally left out because this list is used for matching parses, and decl ref is selected when nothing here matches
const char* _ser_propKindParseNames[] = {
    "char",
    "int",
    "float",
    // "arrIn",
    "arr",
    "ptr",
};

typedef struct _ser_OuterProp _ser_OuterProp;
typedef struct _ser_InnerProp _ser_InnerProp;
typedef struct _ser_Decl _ser_Decl;

struct _ser_InnerProp {
    _ser_PropKind kind;
    _ser_InnerProp* inner; // valid for any non terminal kind - check _ser_isPropKindNonTerminal

    const char* declRefTag; // used to patch the declref ptr after a specset from the user is validated
    int64_t declRefTagLen;
    uint64_t declRefId; // ID for the ref, used for patching the pointer when specs are loaded from files
    _ser_Decl* declRef;

    uint64_t arrLengthParentStructOffset; // in here only because it depends on kind.
};

struct _ser_OuterProp {
    _ser_OuterProp* next;
    _ser_InnerProp inner;
    const char* tag;
    uint64_t tagLen;

    // location of this member inside of the parent struct, from the start, in bytes, used for reading and
    // writing to structs in the program
    uint64_t parentStructOffset;
};
// TODO: refactor props into inner and outer structs

struct _ser_Decl {
    _ser_DeclKind kind;
    _ser_Decl* nextDecl;
    const char* tag;

    // done so that 0 is an invalid index, to make sure that patching references is less error prone
    uint64_t id; // 1 BASED index into the specSet list // filled in on push

    union {
        struct {
            _ser_OuterProp* firstProp;
            uint64_t propCount;
            uint64_t size;
            bool offsetsGiven; // flag for if ser_specOffsets has been called on this
        } structInfo;
        struct {
            uint64_t valCount;
            const char** vals;
        } enumInfo;
    };
};

typedef struct {
    BumpAlloc arena; // TODO: don't embed these as a dep

    _ser_Decl* firstDecl;
    _ser_Decl* lastDecl;
    uint64_t declCount; // calcualted with _ser_declPush, but double checked at validation time
} _ser_SpecSet;

void _ser_clearSpecSet(_ser_SpecSet* set) {
    bump_clear(&set->arena);
    set->firstDecl = NULL;
    set->lastDecl = NULL;
    set->declCount = 0;
}

_ser_SpecSet _ser_globalSpecSet;
bool _ser_isGlobalSetLocked;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SPEC CONSTRUCTION
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

_ser_SpecSet _ser_specSetInit(const char* arenaName) {
    _ser_SpecSet out;
    memset(&out, 0, sizeof(out));
    out.arena = bump_allocate(1000000, arenaName); // TODO: should this be able to grow tho????
    return out;
}

// pushes a new decl, fills the ID as the index, and pushes it to the sets list
_ser_Decl* _ser_declPush(_ser_SpecSet* set) {
    _ser_Decl* s = BUMP_PUSH_NEW(&set->arena, _ser_Decl);
    set->declCount++;
    s->id = set->declCount; // make it 1 indexed // see comment at definition

    // push to the list of decls
    if (!set->firstDecl) {
        set->firstDecl = s;
        set->lastDecl = s;
    } else {
        set->lastDecl->nextDecl = s;
        set->lastDecl = s;
    }
    return s;
}

// null on failure, reports the first seen spec in the set
_ser_Decl* _ser_declGetByTag(_ser_SpecSet* set, const char* tag, uint64_t tagLen) {
    for (_ser_Decl* s = set->firstDecl; s; s = s->nextDecl) {
        if (tagLen == strlen(s->tag)) {
            if (strncmp(tag, s->tag, tagLen) == 0) { // TODO: fuck you stl
                return s;
            }
        }
    }
    return NULL;
}

_ser_Decl* _ser_declGetByID(_ser_SpecSet* set, uint64_t id) {
    // RN just braindead loop until we find the thing w/ the right ID
    for (_ser_Decl* decl = set->firstDecl; decl; decl = decl->nextDecl) {
        if (decl->id == id) {
            return decl;
        }
    }
    return NULL;
}

bool _ser_isPropKindNonTerminal(_ser_PropKind k) {
    if (k == SER_PK_ARRAY_EXTERNAL) {
        return true;
    }
    return false;
}

// DOES NOT CHECK FOR VALID DECL REFS OR OFFSETS OR MODIFY THE SET IN ANY WAY
ser_Error _ser_checkSpecSetDuplicatesCountsKindsInnersAndEmpties(const _ser_SpecSet* set) {
    uint64_t countedDecls = 0; // doing this is very redundant but who cares
    for (_ser_Decl* decl = set->firstDecl; decl; decl = decl->nextDecl) {
        for (_ser_Decl* other = decl->nextDecl; other; other = other->nextDecl) {
            if (strcmp(other->tag, decl->tag) == 0) {
                return SERE_DUPLICATE_DECL_TAGS;
            }
        }
        countedDecls++;

        if (decl->kind == SER_DK_STRUCT) {
            uint64_t countedProps = 0; // this is also very redundant
            for (_ser_OuterProp* outer = decl->structInfo.firstProp; outer; outer = outer->next) {
                _SER_EXPECT(outer->tagLen > 0, SERE_EMPTY_TAG);
                _SER_EXPECT(outer->tag != NULL, SERE_EMPTY_TAG);

                // make sure non terminals have inners, that terminals dont
                for (_ser_InnerProp* inner = &outer->inner; inner; inner = inner->inner) {
                    _SER_EXPECT(inner->kind >= 0 && inner->kind < SER_PK_COUNT, SERE_UNEXPECTED_KIND);
                    if (_ser_isPropKindNonTerminal(inner->kind)) {
                        _SER_EXPECT(inner->inner != NULL, SERE_NONTERMINAL_WITH_NO_INNER);
                    } else {
                        _SER_EXPECT(inner->inner == NULL, SERE_TERMINAL_WITH_INNER);
                        break;
                    }
                }

                // invalidate arrays of arrays
                if (outer->inner.kind == SER_PK_ARRAY_EXTERNAL) {
                    if (outer->inner.inner->kind == SER_PK_ARRAY_EXTERNAL) {
                        return SERE_ARRAY_OF_ARRAYS;
                    }
                }
                // invalidate ptr to an array - cause where would the length live?
                if (outer->inner.kind == SER_PK_PTR) {
                    if (outer->inner.inner->kind == SER_PK_ARRAY_EXTERNAL) {
                        return SERE_PTR_TO_ARRAY;
                    }
                }

                // check duplicates
                for (_ser_OuterProp* other = outer->next; other; other = other->next) {
                    if (other->tagLen == outer->tagLen) {
                        if (strncmp(other->tag, outer->tag, outer->tagLen) == 0) {
                            return SERE_DUPLICATE_PROP_TAGS;
                        }
                    }
                }
                countedProps++;
            }
            _SER_EXPECT(countedProps != 0, SERE_EMPTY_STRUCT_DECL);
            _SER_EXPECT(countedProps == decl->structInfo.propCount, SERE_SPECSET_INCORRECT_PROP_COUNT);
        } else if (decl->kind == SER_DK_ENUM) {
            _SER_EXPECT(decl->enumInfo.vals != NULL, SERE_EMPTY_ENUM);
            _SER_EXPECT(decl->enumInfo.valCount != 0, SERE_EMPTY_ENUM);
            // TODO: check each indiv val for non null
        } else {
            return SERE_UNEXPECTED_KIND;
        }
    }
    _SER_EXPECT(countedDecls != 0, SERE_SPECSET_EMPTY);
    _SER_EXPECT(countedDecls == set->declCount, SERE_SPECSET_INCORRECT_DECL_COUNT);
    return SERE_OK;
}

// patches pointers first based on if there is a tag, if there isn't, does it via ID
// Assumes that _ser_checkSpecSetDuplicatesKindsAndEmpties has been called on the set and was OK.
ser_Error _ser_tryPatchDeclRef(_ser_SpecSet* set, _ser_InnerProp* p) {
    if (p->kind != SER_PK_DECL_REF) {
        if (_ser_isPropKindNonTerminal(p->kind)) {
            return _ser_tryPatchDeclRef(set, p->inner);
        }
        return SERE_OK; // in the case where there is a terminal and it wasn't a SER_PK_DECL_REF node, exit without doing anything
    }

    if (p->declRefTagLen > 0) {
        _ser_Decl* decl = _ser_declGetByTag(set, p->declRefTag, p->declRefTagLen);
        if (decl == NULL) {
            return SERE_INVALID_DECL_REF_TAG;
        }
        p->declRef = decl;
    } else {
        _SER_EXPECT(p->declRefId > 0, SERE_INVALID_DECL_REF_ID);
        _SER_EXPECT(p->declRefId <= set->declCount, SERE_INVALID_DECL_REF_ID); // because 1 indexed, the last ID can be equal to the count
        _ser_Decl* decl = _ser_declGetByID(set, p->declRefId);
        _SER_EXPECT(decl != NULL, SERE_INVALID_DECL_REF_ID);
        p->declRef = decl;
    }
    return SERE_OK;
}

// uses IDs or tags, whichever is present to fill in the declRef ptr in each declRef prop.
// Assumes that _ser_checkSpecSetDuplicatesKindsAndEmpties has been called on the set and was OK.
ser_Error _ser_patchAndCheckSpecSetDeclRefs(_ser_SpecSet* set) {
    // TODO: fail on circular struct composition
    for (_ser_Decl* decl = set->firstDecl; decl; decl = decl->nextDecl) {
        if (decl->kind == SER_DK_STRUCT) {
            for (_ser_OuterProp* prop = decl->structInfo.firstProp; prop; prop = prop->next) {
                ser_Error e = _ser_tryPatchDeclRef(set, &prop->inner);
                _SER_EXPECT_OK(e);
            }
        }
    }
    return SERE_OK;
}

ser_Error _ser_checkOffsetsAreSet(_ser_SpecSet* set) {
    for (_ser_Decl* decl = set->firstDecl; decl; decl = decl->nextDecl) {
        if (decl->kind == SER_DK_STRUCT) {
            _SER_EXPECT(decl->structInfo.offsetsGiven, SERE_NO_OFFSETS);
        }
    }
    return SERE_OK;
}

// public function used to indicate that every spec is done being constructed.
// name is wierd because the user will never have to deal with >1 specset.
// validates and locks the global set // should only be called once in user code
// only locks when checks successful
ser_Error _ser_lockSpecs() {
    _SER_EXPECT(_ser_isGlobalSetLocked == false, SERE_GLOBAL_SET_LOCKED);
    _SER_EXPECT_OK(_ser_checkSpecSetDuplicatesCountsKindsInnersAndEmpties(&_ser_globalSpecSet));
    _SER_EXPECT_OK(_ser_patchAndCheckSpecSetDeclRefs(&_ser_globalSpecSet));
    _SER_EXPECT_OK(_ser_checkOffsetsAreSet(&_ser_globalSpecSet));
    // TODO: non null offset sets check
    _ser_isGlobalSetLocked = true;
    return SERE_OK;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// PARSING && USER SPEC CONSTRUCTION
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
This whole section operates on the global spec set because that's what we need parsing for.
Everything will fail if the global set has already been validated and locked with ser_lockSpecs().
Specs in files don't require this type of string parsing, only the in-code user defined ones.
Also, errors are reported via assert, because code with faulty spec construction has no point in existing - any
user data put into these should really be hardcoded.
*/

// ptr is read and written to // should be inside of a null terminated string
// return is success or failure
// out params unwritten on a failure return
// utility function just for parsing
bool _ser_parseToken(const char** ptr, const char** outTokStart, uint64_t* outTokLen) {
    // skip whitespace // note that this loop will end when it hits a null term
    while (isspace(**ptr)) {
        (*ptr)++;
    }
    if (**ptr == '\0') {
        return false;
    }

    const char* tokStart = (*ptr);
    while (true) {
        (*ptr)++;
        if ((**ptr) == '\0') {
            break;
        } else if (isspace(**ptr)) {
            break;
        }
    }

    *outTokStart = tokStart;
    *outTokLen = (uint64_t)(*ptr - tokStart);
    return tokStart != NULL;
}

// str is a read/write param that represents where parsing is along a null terminated string
// this takes care of arr and ptr needing more tokens after
// allocates into the global sets arena
ser_Error _ser_parsePropInners(const char** str, _ser_InnerProp* prop) {
    const char* kindStr;
    uint64_t kindStrLen;
    _SER_EXPECT(_ser_parseToken(str, &kindStr, &kindStrLen), SERE_PARSE_FAILED);

    _ser_PropKind k = SER_PK_DECL_REF; // default to a decl kind if no keywords match
    const uint64_t parsableCount = sizeof(_ser_propKindParseNames) / sizeof(const char*);
    for (uint64_t i = 0; i < parsableCount; i++) {
        const char* keywordCandidate = _ser_propKindParseNames[i];
        if (strlen(keywordCandidate) == kindStrLen) {
            if (strncmp(_ser_propKindParseNames[i], kindStr, kindStrLen) == 0) {
                k = i;
                break;
            }
        }
    }

    prop->kind = k;
    if (prop->kind == SER_PK_DECL_REF) {
        prop->declRefTag = kindStr;
        prop->declRefTagLen = kindStrLen;
    }

    if (_ser_isPropKindNonTerminal(prop->kind)) {
        prop->inner = BUMP_PUSH_NEW(&_ser_globalSpecSet.arena, _ser_InnerProp);
        _SER_EXPECT_OK(_ser_parsePropInners(str, prop->inner));
    }
    return SERE_OK;
}

ser_Error _ser_specStruct(const char* tag, const char* str) {
    _SER_EXPECT(!_ser_isGlobalSetLocked, SERE_GLOBAL_SET_LOCKED);

    _ser_Decl* decl = _ser_declPush(&_ser_globalSpecSet);
    decl->tag = tag;
    decl->kind = SER_DK_STRUCT;

    _ser_OuterProp* firstProp = NULL;
    _ser_OuterProp* lastProp = NULL;
    int64_t propCount = 0;

    const char* c = str;
    while (true) {
        _ser_OuterProp* prop = BUMP_PUSH_NEW(&_ser_globalSpecSet.arena, _ser_OuterProp);
        bool newToken = _ser_parseToken(&c, &prop->tag, &prop->tagLen);
        if (!newToken) {
            break;
        }
        _SER_EXPECT_OK(_ser_parsePropInners(&c, &prop->inner));

        if (firstProp == NULL) {
            firstProp = prop;
            lastProp = prop;
        } else {
            lastProp->next = prop;
            lastProp = prop;
        }
        propCount++;
    }

    _SER_EXPECT(firstProp != NULL, SERE_EMPTY_STRUCT_DECL);
    decl->structInfo.firstProp = firstProp;
    decl->structInfo.propCount = propCount;
    return SERE_OK;
}

ser_Error _ser_specEnum(const char* tag, const char* strs[], int count) {
    _SER_EXPECT(!_ser_isGlobalSetLocked, SERE_GLOBAL_SET_LOCKED);
    _ser_Decl* s = _ser_declPush(&_ser_globalSpecSet);
    s->tag = tag;
    s->kind = SER_DK_ENUM;
    s->enumInfo.vals = strs;
    s->enumInfo.valCount = count;
    return SERE_OK;
}

// TODO: document how this function works
ser_Error _ser_specStructOffsets(const char* tag, int structSize, int argCount, ...) {
    _SER_EXPECT(!_ser_isGlobalSetLocked, SERE_GLOBAL_SET_LOCKED);
    va_list args;
    va_start(args, argCount);
    int takenCount = 0;

    _ser_Decl* structSpec = _ser_declGetByTag(&_ser_globalSpecSet, tag, strlen(tag));
    _SER_EXPECT(structSpec != NULL, SERE_INVALID_DECL_REF_TAG);
    _SER_EXPECT(structSpec->kind == SER_DK_STRUCT, SERE_UNEXPECTED_KIND);

    structSpec->structInfo.size = structSize;
    structSpec->structInfo.offsetsGiven = true;

    for (_ser_OuterProp* prop = structSpec->structInfo.firstProp; prop; prop = prop->next) {
        int64_t offset = va_arg(args, uint64_t);
        takenCount++;
        _SER_EXPECT(takenCount <= argCount, SERE_VA_ARG_MISUSE);
        prop->parentStructOffset = offset;

        if (prop->inner.kind == SER_PK_ARRAY_EXTERNAL) {
            prop->inner.arrLengthParentStructOffset = va_arg(args, uint64_t);
            takenCount++;
            _SER_EXPECT(takenCount <= argCount, SERE_VA_ARG_MISUSE);
        }
    }

    va_end(args);
    _SER_EXPECT(takenCount == argCount, SERE_VA_ARG_MISUSE);
    return SERE_OK;
}

// used for serializing/deserializing arrays and you need the size of elements in user code
int64_t _ser_sizeOfProp(_ser_InnerProp* p) {
    if (p->kind == SER_PK_CHAR) {
        return sizeof(char);
    } else if (p->kind == SER_PK_INT) {
        return sizeof(int);
    } else if (p->kind == SER_PK_FLOAT) {
        return sizeof(float);
    } else if (p->kind == SER_PK_ARRAY_EXTERNAL) {
        SER_ASSERT(false);
    } else if (p->kind == SER_PK_DECL_REF) {
        if (p->declRef->kind == SER_DK_STRUCT) {
            return p->declRef->structInfo.size;
        } else if (p->declRef->kind == SER_DK_ENUM) {
            return sizeof(int32_t); // TODO: assuming size, this will fuck someone over badly eventually
        } else {
            SER_ASSERT(false);
        }
    } else {
        SER_ASSERT(false);
    }
}

#define _SER_LOOKUP_MEMBER(outT, obj, offset) ((outT*)(((char*)obj) + (offset)))

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SERIALIZATION
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

// TODO: figure out a way to test deserialization on a big endian system
bool _ser_isSystemLittleEndian() {
    uint32_t x = 1;
    return *(char*)(&x) == 1;
}

#define _SER_WRITE_VAR_OR_FAIL(T, var, file)           \
    do {                                               \
        T _var = (T)var;                               \
        _SER_WRITE_OR_FAIL(&_var, sizeof(_var), file); \
    } while(0)

#define _SER_WRITE_OR_FAIL(ptr, size, file)    \
    do {                                       \
        if(fwrite(ptr, size, 1, file) != 1) { \
            return SERE_FWRITE_FAILED;         \
        }                                      \
    } while(0)

/*
The File Format:
all things stored as little endian

uint64_t ser version no
uint64_t app version no
uint64_t declCount
    uint8_t kind // direct from the enum
    uint64_t tagLen
    [tag string]
    if enum:
        uint64_t valCount
            uint64_t val string length
            [val string]
    if struct:
        uint64_t propCount
            uint64_t tag length
            [tag string]
            prop
                    // prop
                        uint8_t kind
                        if declref:
                            uint64_t id
                        if non-terminal:
                            // <- inner prop goes here

uint64_t spec ID (index)
    parse by prop order dictated in the spec :)
    where arrays are a uint64_t for count and then repeated inner elements
    where enums are just int32_ts w the index of the value
*/

typedef struct {
    const void* ptr;
    uint64_t objId;
} _ser_PtrTableElem;

typedef struct {
    BumpAlloc arena;
    _ser_PtrTableElem* elems;
    uint64_t count;
} _ser_PtrTable;

_ser_PtrTable _ser_ptrTableInit() {
    _ser_PtrTable table;
    memset(&table, 0, sizeof(table));
    table.arena = bump_allocate(1000000, "ser ptrTable arena");
    table.elems = table.arena.start;
    return table;
}

_ser_PtrTableElem* _ser_ptrTablePush(_ser_PtrTable* table) {
    _ser_PtrTableElem* p = BUMP_PUSH_NEW(&table->arena, _ser_PtrTableElem);
    table->count++;
    return p;
}

_ser_PtrTableElem* _ser_ptrTableMatchPtr(_ser_PtrTable* table, const void* ptr) {
    for (uint64_t i = 0; i < table->count; i++) {
        if (table->elems[i].ptr == ptr) {
            return &table->elems[i];
        }
    }
    return NULL;
}

// set of the value of every pointer that we want to convert into an objID
// TODO: collect pointed at specs for optimization

ser_Error _ser_ptrTableFindPtrsInDecl(const _ser_Decl* spec, const void* obj, _ser_PtrTable* table);
ser_Error _ser_ptrTableFindPtrsInProp(const _ser_InnerProp* spec, const void* obj, const void* propLoc, _ser_PtrTable* table) {
    if (spec->kind == SER_PK_PTR) {
        // don't duplicate pointers in the table, no more processing is needed here because that pointer has already been followed through
        if (_ser_ptrTableMatchPtr(table, propLoc) != NULL) {
            return SERE_OK;
        }
        _ser_ptrTablePush(table)->ptr = propLoc;
        // TODO: pointer following
    } else if (spec->kind == SER_PK_DECL_REF) {
        if (spec->declRef->kind == SER_DK_STRUCT) {
            _ser_ptrTableFindPtrsInDecl(spec->declRef, propLoc, table);
        }
    } else if (spec->kind == SER_PK_ARRAY_EXTERNAL) {
        const void* arrayStart = propLoc;
        uint64_t count = *_SER_LOOKUP_MEMBER(uint64_t, obj, spec->arrLengthParentStructOffset);
        uint64_t innerSize = _ser_sizeOfProp(spec->inner);
        for (uint64_t i = 0; i < count; i++) {
            const void* elementPtr = _SER_LOOKUP_MEMBER(void, arrayStart, i * innerSize);
            _ser_ptrTableFindPtrsInProp(spec->inner, NULL, elementPtr, table);
        }
    }
    return SERE_OK;
}

// most segfault prone thing i have ever seen in my life
// expects the entire specset that it is working on to be validated completely
ser_Error _ser_ptrTableFindPtrsInDecl(const _ser_Decl* spec, const void* obj, _ser_PtrTable* table) {
    _SER_EXPECT(spec->kind == SER_DK_STRUCT, SERE_UNEXPECTED_KIND);
    for (_ser_OuterProp* prop = spec->structInfo.firstProp; prop; prop = prop->next) {
        const void* propLoc = _SER_LOOKUP_MEMBER(void, obj, prop->parentStructOffset);
        _SER_EXPECT_OK(_ser_ptrTableFindPtrsInProp(&prop->inner, obj, propLoc, table));
    }
    return SERE_OK;
}

// fills in the ptrTable elment on a match, increments currentObjId regardless
void _ser_ptrTableTryFillObjId(const void* obj, uint64_t* currentObjId, _ser_PtrTable* table) {
    _ser_PtrTableElem* matched = _ser_ptrTableMatchPtr(table, obj);
    if (matched != NULL) {
        matched->objId = *currentObjId;
    }
    (*currentObjId)++;
}

// expects the entire specset that it is working on to be validated completely
// requires the table to already have all pointer filled in
// TODO: typecheck matches // made slightly more difficult by the fact that pointers pointing to the first prop
// in a struct are equal to those pointing at the struct itself. Right now only one of these would be entered,
// so just tacking on a spec to each doesn't work.
// TODO: constify fn parameters
ser_Error _ser_ptrTableFindIDs(_ser_Decl* spec, void* obj, _ser_PtrTable* table, uint64_t* currentObjId) {
    _SER_EXPECT(spec->kind == SER_DK_STRUCT, SERE_UNEXPECTED_KIND);
    _ser_ptrTableTryFillObjId(obj, currentObjId, table);

    for (_ser_OuterProp* prop = spec->structInfo.firstProp; prop; prop = prop->next) {
        void* propLoc = _SER_LOOKUP_MEMBER(void, obj, prop->parentStructOffset);
        _ser_ptrTableTryFillObjId(propLoc, currentObjId, table);
        if (prop->inner.kind == SER_PK_ARRAY_EXTERNAL) {
            void* arrayStart = *_SER_LOOKUP_MEMBER(void*, obj, prop->parentStructOffset);
            uint64_t count = *_SER_LOOKUP_MEMBER(uint64_t, obj, prop->inner.arrLengthParentStructOffset);
            uint64_t innerSize = _ser_sizeOfProp(prop->inner.inner);
            _ser_InnerProp* innerElemSpec = prop->inner.inner;
            bool innerIsStruct = innerElemSpec->kind == SER_PK_DECL_REF;
            innerIsStruct = innerIsStruct && innerElemSpec->declRef->kind == SER_DK_STRUCT;
            for (uint64_t i = 0; i < count; i++) {
                void* elemPos = _SER_LOOKUP_MEMBER(void, arrayStart, i * innerSize);
                _ser_ptrTableTryFillObjId(elemPos, currentObjId, table);
                if (innerIsStruct) { // only structs contain more things inside them that could match pointers
                    _ser_ptrTableFindIDs(prop->inner.inner->declRef, elemPos, table, currentObjId);
                }
            }
        }
    }
    return SERE_OK;
}

ser_Error _ser_serializeObjByDeclSpec(FILE* file, _ser_PtrTable* ptrTable, void* obj, _ser_Decl* spec);

// used on everything except arrays, because those need access to the OG object, not just the location of the array
ser_Error _ser_serializeObjByInner(FILE* file, _ser_PtrTable* ptrTable, void* propLoc, _ser_InnerProp* spec) {
    if (spec->kind == SER_PK_FLOAT) {
        _SER_WRITE_OR_FAIL(propLoc, sizeof(float), file);
    } else if (spec->kind == SER_PK_INT) {
        _SER_WRITE_OR_FAIL(propLoc, sizeof(int), file);
    } else if (spec->kind == SER_PK_CHAR) {
        _SER_WRITE_OR_FAIL(propLoc, sizeof(char), file);
    } else if (spec->kind == SER_PK_DECL_REF) {
        return _ser_serializeObjByDeclSpec(file, ptrTable, propLoc, spec->declRef);
    } else if (spec->kind == SER_PK_PTR) {
        _ser_PtrTableElem* elem = _ser_ptrTableMatchPtr(ptrTable, *(void**)propLoc);
        _SER_EXPECT(elem != NULL, SERE_UNRESOLVED_PTR);
        _SER_WRITE_VAR_OR_FAIL(uint64_t, elem->objId, file);
    } else {
        SER_ASSERT(false);
    }
    // array inners should never be other arrays
    return SERE_OK;
}

ser_Error _ser_serializeObjByOuter(FILE* file, _ser_PtrTable* ptrTable, void* obj, _ser_OuterProp* prop) {
    if (prop->inner.kind == SER_PK_ARRAY_EXTERNAL) {
        // TODO: assuming type, will fuck w you later
        uint64_t arrCount = *_SER_LOOKUP_MEMBER(uint64_t, obj, prop->inner.arrLengthParentStructOffset);
        _SER_WRITE_VAR_OR_FAIL(uint64_t, arrCount, file);

        int64_t innerSize = _ser_sizeOfProp(prop->inner.inner);
        void* arrayStart = *_SER_LOOKUP_MEMBER(void*, obj, prop->parentStructOffset);
        for (uint64_t i = 0; i < arrCount; i++) {
            void* ptr = _SER_LOOKUP_MEMBER(void, arrayStart, (i * innerSize));
            // calling the inner works because inner parent offsets are 0 normally
            ser_Error e = _ser_serializeObjByInner(file, ptrTable, ptr, prop->inner.inner);
            _SER_EXPECT_OK(e);
        }
    } else {
        void* propLoc = _SER_LOOKUP_MEMBER(void, obj, prop->parentStructOffset);
        _SER_EXPECT_OK(_ser_serializeObjByInner(file, ptrTable, propLoc, &prop->inner));
    }
    return SERE_OK;
}

// TODO: enums are assuming adjacent, incrementing from zero values - this isn't correct in some cases
// should be documented or fixed

ser_Error _ser_serializeObjByDeclSpec(FILE* file, _ser_PtrTable* ptrTable, void* obj, _ser_Decl* spec) {
    if (spec->kind == SER_DK_STRUCT) {
        for (_ser_OuterProp* prop = spec->structInfo.firstProp; prop; prop = prop->next) {
            _ser_serializeObjByOuter(file, ptrTable, obj, prop);
        }
    } else if (spec->kind == SER_DK_ENUM) {
        _SER_WRITE_OR_FAIL(obj, sizeof(int32_t), file);
    } else {
        SER_ASSERT(false);
    }
    return SERE_OK;
}

ser_Error _ser_serializeInnerProp(FILE* file, _ser_InnerProp* prop) {
    SER_ASSERT(prop->kind < 255 && prop->kind >= 0);
    _SER_WRITE_VAR_OR_FAIL(uint8_t, prop->kind, file);

    if (prop->kind == SER_PK_DECL_REF) {
        _SER_WRITE_VAR_OR_FAIL(uint64_t, prop->declRef->id, file);
    }

    if (_ser_isPropKindNonTerminal(prop->kind)) {
        _ser_serializeInnerProp(file, prop->inner);
    }
    return SERE_OK;
}

// TODO: remove as many asserts as possible

// removed from writeObj because the early returns would stop closing the file
ser_Error _ser_writeObjInner(FILE* file, _ser_PtrTable* table, void* obj, const char* specTag) {
    _ser_Decl* spec = _ser_declGetByTag(&_ser_globalSpecSet, specTag, strlen(specTag));
    _SER_EXPECT(spec != NULL, SERE_INVALID_DECL_REF_TAG);
    _SER_EXPECT_OK(_ser_ptrTableFindPtrsInDecl(spec, obj, table));
    uint64_t curObjId = 1;
    _ser_ptrTableFindIDs(spec, obj, table, &curObjId);

    _SER_WRITE_VAR_OR_FAIL(uint64_t, 0, file); // ser version indicator
    _SER_WRITE_VAR_OR_FAIL(uint64_t, 0, file); // app version indicator
    _SER_WRITE_VAR_OR_FAIL(uint64_t, _ser_globalSpecSet.declCount, file);

    for (_ser_Decl* decl = _ser_globalSpecSet.firstDecl; decl; decl = decl->nextDecl) {
        SER_ASSERT(decl->kind < 255 && decl->kind >= 0);
        _SER_WRITE_VAR_OR_FAIL(uint8_t, decl->kind, file);

        uint64_t tagLen = strlen(decl->tag);
        _SER_WRITE_VAR_OR_FAIL(uint64_t, tagLen, file);
        _SER_WRITE_OR_FAIL(decl->tag, tagLen, file);

        if (decl->kind == SER_DK_ENUM) {
            _SER_WRITE_VAR_OR_FAIL(uint64_t, decl->enumInfo.valCount, file);
            for (uint64_t enumValIdx = 0; enumValIdx < decl->enumInfo.valCount; enumValIdx++) {
                const char* val = decl->enumInfo.vals[enumValIdx];
                uint64_t valStrLen = strlen(val);
                _SER_WRITE_VAR_OR_FAIL(uint64_t, valStrLen, file);
                _SER_WRITE_OR_FAIL(val, valStrLen, file);
            }
        } else if (decl->kind == SER_DK_STRUCT) {
            _SER_WRITE_VAR_OR_FAIL(uint64_t, decl->structInfo.propCount, file);
            for (_ser_OuterProp* prop = decl->structInfo.firstProp; prop; prop = prop->next) {
                SER_ASSERT(prop->tagLen != 0);
                _SER_WRITE_VAR_OR_FAIL(uint64_t, prop->tagLen, file);
                _SER_WRITE_OR_FAIL(prop->tag, prop->tagLen, file);
                _SER_EXPECT_OK(_ser_serializeInnerProp(file, &prop->inner));
            }
        } else {
            SER_ASSERT(false);
        }
    }

    _SER_WRITE_VAR_OR_FAIL(uint64_t, spec->id, file);
    _SER_EXPECT_OK(_ser_serializeObjByDeclSpec(file, table, obj, spec));
    return SERE_OK;
}

ser_Error _ser_writeObj(const char* specTag, void* obj, const char* path) {
    _SER_EXPECT(_ser_isGlobalSetLocked, SERE_GLOBAL_SET_UNLOCKED);
    FILE* file = fopen(path, "wb");
    _SER_EXPECT(file != NULL, SERE_FOPEN_FAILED);
    _ser_PtrTable ptrTable = _ser_ptrTableInit();

    ser_Error e = _ser_writeObjInner(file, &ptrTable, obj, specTag);

    bump_free(&ptrTable.arena);
    fclose(file);
    return e;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// DESERIALIZATION !?
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

// quotes on the static assert here because the linter was tripping
// makes sure that var and T are the same size
// double evals T and var
#define _SER_READ_VAR_OR_FAIL(T, var, file)                                             \
    do {                                                                                \
        static_assert(sizeof(var) >= sizeof(T), "");                                    \
        _SER_EXPECT(fread(&(var), sizeof(T), 1, file) == 1, SERE_FREAD_FAILED);  \
    } while(0)

#define _SER_READ_OR_FAIL(buffer, size, file) \
    _SER_EXPECT(fread(buffer, size, 1, file) == 1, SERE_FREAD_FAILED)

typedef struct {
    FILE* file;
    _ser_SpecSet specSet;
    BumpAlloc* outArena;

    uint64_t fileSerVersion;
    uint64_t fileVersion;
} _ser_DserInstance;

// fills in kinds and aux info for the first prop and any inners
// props allocated to the specsets arena
ser_Error _ser_dserPropInner(_ser_DserInstance* inst, _ser_InnerProp* first) {
    uint8_t kind = 0;
    _SER_READ_VAR_OR_FAIL(uint8_t, kind, inst->file);
    _SER_EXPECT(kind < SER_PK_COUNT, SERE_UNEXPECTED_KIND);
    first->kind = kind;

    if (_ser_isPropKindNonTerminal(first->kind)) {
        _ser_InnerProp* inner = BUMP_PUSH_NEW(&inst->specSet.arena, _ser_InnerProp);
        _SER_EXPECT_OK(_ser_dserPropInner(inst, inner));
        SER_ASSERT(inner != NULL);
        first->inner = inner;
    } else if (first->kind == SER_PK_DECL_REF) {
        _SER_READ_VAR_OR_FAIL(uint64_t, first->declRefId, inst->file);
    }
    return SERE_OK;
}

// allocates tags into the specset arena
// moves the inst filestream forwards
ser_Error _ser_dserDecl(_ser_DserInstance* inst) {
    _ser_Decl* decl = _ser_declPush(&inst->specSet);

    uint8_t kind = 0;
    _SER_READ_VAR_OR_FAIL(uint8_t, kind, inst->file);
    decl->kind = kind;

    uint64_t strLen = 0;
    _SER_READ_VAR_OR_FAIL(uint64_t, strLen, inst->file);
    char* str = BUMP_PUSH_ARR(&inst->specSet.arena, strLen + 1, char); // add one for the null term >:(
    _SER_READ_OR_FAIL(str, strLen, inst->file);
    decl->tag = str;

    if (decl->kind == SER_DK_STRUCT) {
        _SER_READ_VAR_OR_FAIL(uint64_t, decl->structInfo.propCount, inst->file);
        _ser_OuterProp* lastProp = NULL;

        for (uint64_t i = 0; i < decl->structInfo.propCount; i++) {
            _ser_OuterProp* p = BUMP_PUSH_NEW(&inst->specSet.arena, _ser_OuterProp);
            _SER_READ_VAR_OR_FAIL(uint64_t, p->tagLen, inst->file);
            char* tagBuf = BUMP_PUSH_ARR(&inst->specSet.arena, p->tagLen + 1, char); // add one for the null term >:(
            _SER_READ_OR_FAIL(tagBuf, p->tagLen, inst->file);
            p->tag = tagBuf;
            _SER_EXPECT_OK(_ser_dserPropInner(inst, &p->inner));

            // push to the back of the prop list
            if (!lastProp) {
                decl->structInfo.firstProp = p;
                lastProp = p;
            } else {
                lastProp->next = p;
                lastProp = p;
            }
        }
    } else if (decl->kind == SER_DK_ENUM) {
        assert(false);
    } else {
        return SERE_UNEXPECTED_KIND;
    }

    return SERE_OK;
}

// both of these are expecting that the decl/prop has been fully filled in for deserialization // offsets done, links completed etc.
ser_Error _ser_readToObjFromDecl(void* obj, _ser_Decl* decl, FILE* file, BumpAlloc* arena);
ser_Error _ser_readToObjFromProp(void* obj, _ser_OuterProp* prop, FILE* file, BumpAlloc* arena);

ser_Error _ser_readToObjFromDecl(void* obj, _ser_Decl* decl, FILE* file, BumpAlloc* arena) {
    if (decl->kind == SER_DK_STRUCT) {
        for (_ser_OuterProp* prop = decl->structInfo.firstProp; prop; prop = prop->next) {
            _SER_EXPECT_OK(_ser_readToObjFromProp(obj, prop, file, arena));
        }
    } else if (decl->kind == SER_DK_ENUM) {
        // TODO: when enum reordering gets added - see if this changes at all
        // TODO: assuming int32_t is gonna be really upsetting at some point
        _SER_READ_OR_FAIL(obj, sizeof(int32_t), file);
    } else {
        SER_ASSERT(false);
    }
    return SERE_OK;
}

ser_Error _ser_readToPropFromInner(void* loc, _ser_InnerProp* prop, FILE* file, BumpAlloc* arena) {
    if (prop->kind == SER_PK_CHAR) {
        _SER_READ_OR_FAIL(loc, sizeof(uint8_t), file);
    } else if (prop->kind == SER_PK_INT) {
        _SER_READ_OR_FAIL(loc, sizeof(int), file);
    } else if (prop->kind == SER_PK_FLOAT) {
        _SER_READ_OR_FAIL(loc, sizeof(float), file);
    } else if (prop->kind == SER_PK_DECL_REF) {
        _SER_EXPECT_OK(_ser_readToObjFromDecl(loc, prop->declRef, file, arena));
    } else {
        SER_ASSERT(false);
    }
    // arrays of arrays shouldn't exist
    return SERE_OK;
}

// each prop needs to offset itself into the struct (bc array has to reference two different things)
ser_Error _ser_readToObjFromProp(void* obj, _ser_OuterProp* prop, FILE* file, BumpAlloc* arena) {
    if (prop->inner.kind == SER_PK_ARRAY_EXTERNAL) {
        uint64_t count = 0;
        _SER_READ_VAR_OR_FAIL(uint64_t, count, file);
        *_SER_LOOKUP_MEMBER(uint64_t, obj, prop->inner.arrLengthParentStructOffset) = count; // TODO: type assumption will be bad at some point
        uint64_t innerSize = _ser_sizeOfProp(prop->inner.inner);
        void* array = bump_push(arena, innerSize * count);
        *_SER_LOOKUP_MEMBER(void*, obj, prop->parentStructOffset) = array;
        for (uint64_t i = 0; i < count; i++) {
            void* loc = _SER_LOOKUP_MEMBER(void*, array, i * innerSize);
            _SER_EXPECT_OK(_ser_readToPropFromInner(loc, prop->inner.inner, file, arena));
        }
    } else {
        void* propLoc = _SER_LOOKUP_MEMBER(void, obj, prop->parentStructOffset);
        _ser_readToPropFromInner(propLoc, &prop->inner, file, arena);
    }
    return SERE_OK;
}

// split from OG readObj to make sure the file doesn't leak on an error
ser_Error _ser_readObjFromFileInner(_ser_DserInstance* inst, const char* type, void* obj) {
    // deserialize versions and spec, check it
    {
        _SER_READ_VAR_OR_FAIL(uint64_t, inst->fileSerVersion, inst->file);
        _SER_READ_VAR_OR_FAIL(uint64_t, inst->fileVersion, inst->file);

        uint64_t declCount = 0;
        // TODO: REASONABLE LIMITS FOR LENGTHS ON DESERIALIZED INFO??
        _SER_READ_VAR_OR_FAIL(uint64_t, declCount, inst->file);
        for (uint64_t i = 0; i < declCount; i++) {
            _SER_EXPECT_OK(_ser_dserDecl(inst));
        }

        _SER_EXPECT_OK(_ser_checkSpecSetDuplicatesCountsKindsInnersAndEmpties(&inst->specSet));
        _SER_EXPECT_OK(_ser_patchAndCheckSpecSetDeclRefs(&inst->specSet));
    }

    // TODO: rn checking that current and old specs exactly match, then copying over offsets, add more flex
    // decl reordering, enum reordering, prop reordering, removed props, added props, etc.
    {
        _SER_EXPECT(_ser_globalSpecSet.declCount == inst->specSet.declCount, SERE_SPEC_MISMATCH);
        _ser_Decl* globalDecl = _ser_globalSpecSet.firstDecl;
        _ser_Decl* fileDecl = inst->specSet.firstDecl;
        while (true) {
            // same tags
            _SER_EXPECT(strcmp(globalDecl->tag, fileDecl->tag) == 0, SERE_SPEC_MISMATCH);
            _SER_EXPECT(globalDecl->kind == fileDecl->kind, SERE_SPEC_MISMATCH);

            if (globalDecl->kind == SER_DK_STRUCT) {
                _SER_EXPECT(globalDecl->structInfo.propCount == fileDecl->structInfo.propCount, SERE_SPEC_MISMATCH);
                fileDecl->structInfo.size = globalDecl->structInfo.size;
                _ser_OuterProp* globalProp = globalDecl->structInfo.firstProp;
                _ser_OuterProp* fileProp = fileDecl->structInfo.firstProp;
                while (true) {
                    _SER_EXPECT(globalProp->tagLen == fileProp->tagLen, SERE_SPEC_MISMATCH);
                    _SER_EXPECT(strncmp(globalProp->tag, fileProp->tag, globalProp->tagLen) == 0, SERE_SPEC_MISMATCH);

                    fileProp->parentStructOffset = globalProp->parentStructOffset;

                    // make sure all inner kinds and inner info match // copy inner offsets for arrays
                    {
                        // we can assume both have correct inners because of checks from above
                        _ser_InnerProp* globalInner = &globalProp->inner;
                        _ser_InnerProp* fileInner = &fileProp->inner;
                        while (true) {
                            _SER_EXPECT(globalInner->kind == fileInner->kind, SERE_SPEC_MISMATCH);
                            if (globalInner->kind == SER_PK_DECL_REF) {
                                _SER_EXPECT(strcmp(globalInner->declRef->tag, fileInner->declRef->tag) == 0, SERE_SPEC_MISMATCH);
                            } else if (globalInner->kind == SER_PK_ARRAY_EXTERNAL) {
                                fileInner->arrLengthParentStructOffset = globalInner->arrLengthParentStructOffset;
                            }

                            globalInner = globalInner->inner;
                            fileInner = fileInner->inner;
                            if (globalInner == NULL) {
                                break;
                            }
                        }
                    }
                    // assume both end at the same time because of previous checks
                    globalProp = globalProp->next;
                    fileProp = fileProp->next;
                    if (globalProp == NULL) {
                        break;
                    }
                }
            } else if (globalDecl->kind == SER_DK_ENUM) {
                _SER_EXPECT(globalDecl->enumInfo.valCount == fileDecl->enumInfo.valCount, SERE_SPEC_MISMATCH);
                for (uint64_t i = 0; i < globalDecl->enumInfo.valCount; i++) {
                    _SER_EXPECT(strcmp(globalDecl->enumInfo.vals[i], fileDecl->enumInfo.vals[i]) == 0, SERE_SPEC_MISMATCH);
                }
            } else {
                SER_ASSERT(false);
            }

            globalDecl = globalDecl->nextDecl;
            fileDecl = fileDecl->nextDecl;
            // assume both end at the same time because we checked that counts are accurate and that both have the same count
            if (globalDecl == NULL) {
                break;
            }
        }
    }

    // now that the spec from the file has been filled in properly to dserialize data into structs, do it
    _ser_Decl* targetSpec = _ser_declGetByTag(&inst->specSet, type, strlen(type));
    uint64_t targetSpecID = 0;
    _SER_READ_VAR_OR_FAIL(uint64_t, targetSpecID, inst->file);
    _SER_EXPECT(targetSpecID == targetSpec->id, SERE_INVALID_PULL_TYPE);
    SER_ASSERT(targetSpec->kind == SER_DK_STRUCT);
    _SER_EXPECT_OK(_ser_readToObjFromDecl(obj, targetSpec, inst->file, inst->outArena));
    return SERE_OK;
}

// TODO: file patches! // TODO: can we embed backwards compatibility things too?
ser_Error _ser_readObjFromFile(const char* type, void* obj, const char* path, BumpAlloc* outArena) {
    _SER_EXPECT(_ser_isGlobalSetLocked, SERE_GLOBAL_SET_UNLOCKED);

    _ser_DserInstance inst;
    memset(&inst, 0, sizeof(_ser_DserInstance));
    inst.specSet = _ser_specSetInit("dser inst spec set arena");
    inst.file = fopen(path, "rb");
    _SER_EXPECT(inst.file != NULL, SERE_FOPEN_FAILED);
    inst.outArena = outArena;

    ser_Error e = _ser_readObjFromFileInner(&inst, type, obj);

    // TODO: this reasource leaks on errors rn, probably should fix that
    if (inst.file != NULL) {
        fclose(inst.file);
    }
    bump_free(&inst.specSet.arena);
    return e;
}

#define _SER_STRINGIZE(x) #x
#define ser_specStruct(T, str) SER_ASSERT_OK(_ser_specStruct(_SER_STRINGIZE(T), _SER_STRINGIZE(str)))
#define ser_specEnum(T, strs, count) SER_ASSERT_OK(_ser_specEnum(_SER_STRINGIZE(T), strs, count))
#define ser_lockSpecs() SER_ASSERT_OK(_ser_lockSpecs())
// TODO: check that ptr and T are the same type
#define ser_writeObj(T, ptr, path) SER_ASSERT_OK(_ser_writeObj(_SER_STRINGIZE(T), ptr, path))
#define ser_readObj(T, ptr, path, arena) SER_ASSERT_OK(_ser_readObjFromFile(_SER_STRINGIZE(T), ptr, path, arena))

// built becuase the linter was throwing a fit
#define _SER_OFFSETOF(T, prop) ((uint64_t)(uint8_t*)(&(((T*)(NULL))->prop)))

#define _SER_OFFSET2(T, a) _SER_OFFSETOF(T, a)
#define _SER_OFFSET3(T, a, b) _SER_OFFSETOF(T, a), _SER_OFFSETOF(T, b)
#define _SER_OFFSET4(T, a, b, c) _SER_OFFSETOF(T, a), _SER_OFFSETOF(T, b), _SER_OFFSETOF(T, c)
#define _SER_OFFSET5(T, a, b, c, d) _SER_OFFSETOF(T, a), _SER_OFFSETOF(T, b), _SER_OFFSETOF(T, c), _SER_OFFSETOF(T, d)
#define _SER_OFFSET6(T, a, b, c, d, e) _SER_OFFSETOF(T, a), _SER_OFFSETOF(T, b), _SER_OFFSETOF(T, c), _SER_OFFSETOF(T, d), _SER_OFFSETOF(T, e)
#define _SER_OFFSET7(T, a, b, c, d, e, f) _SER_OFFSETOF(T, a), _SER_OFFSETOF(T, b), _SER_OFFSETOF(T, c), _SER_OFFSETOF(T, d), _SER_OFFSETOF(T, e), _SER_OFFSETOF(T, f)
#define _SER_OFFSET8(T, a, b, c, d, e, f, g) _SER_OFFSETOF(T, a), _SER_OFFSETOF(T, b), _SER_OFFSETOF(T, c), _SER_OFFSETOF(T, d), _SER_OFFSETOF(T, e), _SER_OFFSETOF(T, f), _SER_OFFSETOF(T, g)

#define _SER_SELECT_BY_PARAM_COUNT(_1,_2,_3,_4,_5,_6,_7,_8,NAME,...) NAME
#define _SER_OFFSET_SWITCH(...) _SER_SELECT_BY_PARAM_COUNT(__VA_ARGS__, _SER_OFFSET8, _SER_OFFSET7, _SER_OFFSET6, _SER_OFFSET5, _SER_OFFSET4, _SER_OFFSET3, _SER_OFFSET2)(__VA_ARGS__)
#define _SER_ARG_COUNT_SWITCH(...) _SER_SELECT_BY_PARAM_COUNT(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2)
#define ser_specStructOffsets(T, ...) SER_ASSERT_OK(_ser_specStructOffsets(_SER_STRINGIZE(T), sizeof(T), _SER_ARG_COUNT_SWITCH(T, __VA_ARGS__) - 1, _SER_OFFSET_SWITCH(T, __VA_ARGS__)))

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TESTING
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "HMM/HandmadeMath.h"
#include "sketches.h"

// reads out 1 T from the file, compares it to expected, on failure returns with a SERE_TEST_EXPECT_FAILED
#define _SER_TEST_READ(T, expected, file)                                      \
    do {                                                                       \
        T _got;                                                                \
        _SER_EXPECT(fread(&_got, sizeof(T), 1, file) == 1, SERE_FREAD_FAILED); \
        _SER_EXPECT(_got == (T)expected, SERE_TEST_EXPECT_FAILED);                \
    } while(0)

ser_Error _ser_test_expectString(const char* expected, uint64_t expectedLen, FILE* file) {
    SER_ASSERT(expectedLen != 0);
    char* got = malloc(expectedLen);
    SER_ASSERT(got);
    _SER_EXPECT(fread(got, expectedLen, 1, file) == 1, SERE_FREAD_FAILED);
    _SER_EXPECT(memcmp(got, expected, expectedLen) == 0, SERE_TEST_EXPECT_FAILED);
    free(got);
    return SERE_OK;
}

// clear old assert definition so that tests can not exit on a failure
#undef SER_ASSERT_OK
#define SER_ASSERT_OK(expr) expr
// TODO: this fucks w/ other files downstream

ser_Error _ser_test_serializeVec2() {
    _SER_EXPECT_OK(
        ser_specStruct(HMM_Vec2,
                       X float
                       Y float));
    _SER_EXPECT_OK(ser_specStructOffsets(HMM_Vec2, X, Y));
    _SER_EXPECT_OK(ser_lockSpecs());

    HMM_Vec2 v = HMM_V2(69, 420);
    _SER_EXPECT_OK(ser_writeObj(HMM_Vec2, &v, "./testing/serializeVec2"));

    FILE* f = fopen("./testing/serializeVec2", "rb");
    _SER_EXPECT(f != NULL, SERE_FOPEN_FAILED);

    _SER_TEST_READ(uint64_t, 0, f); // ser version
    _SER_TEST_READ(uint64_t, 0, f); // app version
    _SER_TEST_READ(uint64_t, 1, f); // spec count

    _SER_TEST_READ(uint8_t, SER_DK_STRUCT, f); // first spec is a struct
    _SER_TEST_READ(uint64_t, 8, f); // length of name str
    _SER_EXPECT_OK(_ser_test_expectString("HMM_Vec2", 8, f)); // name
    _SER_TEST_READ(uint64_t, 2, f); // prop count

    _SER_TEST_READ(uint64_t, 1, f); // tag len
    _SER_EXPECT_OK(_ser_test_expectString("X", 1, f));
    _SER_TEST_READ(uint8_t, SER_PK_FLOAT, f); // kind should be a float

    _SER_TEST_READ(uint64_t, 1, f); // tag len
    _SER_EXPECT_OK(_ser_test_expectString("Y", 1, f));
    _SER_TEST_READ(uint8_t, SER_PK_FLOAT, f); // kind should be a float

    _SER_TEST_READ(uint64_t, 1, f); // origin spec should be ID 1, the vector
    _SER_TEST_READ(float, 69, f);
    _SER_TEST_READ(float, 420, f);

    uint8_t eofProbe = 0;
    _SER_EXPECT(fread(&eofProbe, sizeof(uint8_t), 1, f) == 0, SERE_TEST_EXPECT_FAILED);

    fclose(f);
    return SERE_OK;
}

typedef struct {
    char pad;
    uint64_t count;
    void* elems;
} _ser_test_arrayStruct;

ser_Error _ser_test_multipleVec2RoundTrip() {
    _SER_EXPECT_OK(
        ser_specStruct(HMM_Vec2,
                       X float
                       Y float));
    _SER_EXPECT_OK(ser_specStructOffsets(HMM_Vec2, X, Y));
    _SER_EXPECT_OK(
        ser_specStruct(_ser_test_arrayStruct,
                       vecs arr HMM_Vec2));
    _SER_EXPECT_OK(ser_specStructOffsets(_ser_test_arrayStruct, elems, count));
    _SER_EXPECT_OK(ser_lockSpecs());

    HMM_Vec2 vecs[] = {
        HMM_V2(1, 2),
        HMM_V2(3, 4),
        HMM_V2(10, 20),
        HMM_V2(13, 24),
        HMM_V2(69, 420)
    };
    _ser_test_arrayStruct vecArr = { .elems = vecs, .count = 5 };
    _SER_EXPECT_OK(ser_writeObj(_ser_test_arrayStruct, &vecArr, "./testing/v2RoundTrip"));

    _ser_test_arrayStruct outArr;
    memset(&outArr, 0, sizeof(outArr));
    BumpAlloc arena = bump_allocate(1000000, "v2 round trip arena");
    _SER_EXPECT_OK(ser_readObj(_ser_test_arrayStruct, &outArr, "./testing/v2RoundTrip", &arena));

    _SER_EXPECT(outArr.count == vecArr.count, SERE_TEST_EXPECT_FAILED);
    HMM_Vec2* outVecs = (HMM_Vec2*)outArr.elems;
    for (uint64_t i = 0; i < outArr.count; i++) {
        _SER_EXPECT(outVecs[i].X == vecs[i].X && outVecs[i].Y == vecs[i].Y, SERE_TEST_EXPECT_FAILED);
    }
    bump_free(&arena);
    return SERE_OK;
}

ser_Error _ser_test_shortStructSpec() {
    _SER_EXPECT(
        ser_specStruct(myStruct,
                       X float
                       Y int
                       Z arr) == SERE_PARSE_FAILED,
        SERE_TEST_EXPECT_FAILED);
    return SERE_OK;
}

ser_Error _ser_test_badStructOffsetsCall() {
    _SER_EXPECT_OK(ser_specStruct(HMM_Vec2, X float Y float));
    _SER_EXPECT(
        ser_specStructOffsets(HMM_Vec2, X) == SERE_VA_ARG_MISUSE,
        SERE_TEST_EXPECT_FAILED);
    return SERE_OK;
}

ser_Error _ser_test_duplicateStructProps() {
    _SER_EXPECT_OK(
        ser_specStruct(HMM_Vec2,
                       x int
                       x char));
    _SER_EXPECT_OK(ser_specStructOffsets(HMM_Vec2, X, Y));
    _SER_EXPECT(
        ser_lockSpecs() == SERE_DUPLICATE_PROP_TAGS,
        SERE_TEST_EXPECT_FAILED);
    return SERE_OK;
}

ser_Error _ser_test_duplicateDecls() {
    _SER_EXPECT_OK(ser_specStruct(myStruct, x int));
    const char* vals[] = { "hello" };
    _SER_EXPECT_OK(ser_specEnum(myStruct, vals, 1));
    _SER_EXPECT(
        ser_lockSpecs() == SERE_DUPLICATE_DECL_TAGS,
        SERE_TEST_EXPECT_FAILED);
    return SERE_OK;
}

ser_Error _ser_test_emptyEnum() {
    const char* vals[] = { "hello" };
    _SER_EXPECT_OK(ser_specEnum(enum, vals, 0));
    _SER_EXPECT(
        ser_lockSpecs() == SERE_EMPTY_ENUM,
        SERE_TEST_EXPECT_FAILED);
    return SERE_OK;
}

ser_Error _ser_test_emptyEnum2() {
    _SER_EXPECT_OK(ser_specEnum(enum, NULL, 10));
    ser_Error e = ser_lockSpecs();
    _SER_EXPECT(e == SERE_EMPTY_ENUM, SERE_TEST_EXPECT_FAILED);
    return SERE_OK;
}

ser_Error _ser_test_unresolvedDeclRef() {
    ser_Error e = ser_specStruct(HMM_Vec2,
                                 next struct2
                                 prev struct1);
    _SER_EXPECT_OK(e);
    _SER_EXPECT_OK(ser_specStructOffsets(HMM_Vec2, X, Y));

    e = ser_lockSpecs();
    _SER_EXPECT(e == SERE_INVALID_DECL_REF_TAG, SERE_TEST_EXPECT_FAILED);
    return SERE_OK;
}

#define _SER_TEST_INVOKE(func) _ser_test_invoke(func, _SER_STRINGIZE(func))
typedef ser_Error(*_ser_testFunc)();
void _ser_test_invoke(_ser_testFunc func, const char* name) {
    // reset the global spec so each test can run cleanly
    _ser_clearSpecSet(&_ser_globalSpecSet);
    _ser_isGlobalSetLocked = false;

    ser_Error e = func();
    test_printResult(e == SERE_OK, name);
    if (e != SERE_OK) {
        printf("\tError code: %d\n", e);
    }
}

void ser_tests() {
    test_printSectionHeader("Ser");

    _ser_globalSpecSet = _ser_specSetInit("global spec set arena");
    _SER_TEST_INVOKE(_ser_test_serializeVec2);
    _SER_TEST_INVOKE(_ser_test_multipleVec2RoundTrip);
    _SER_TEST_INVOKE(_ser_test_shortStructSpec);
    _SER_TEST_INVOKE(_ser_test_badStructOffsetsCall);
    _SER_TEST_INVOKE(_ser_test_duplicateStructProps);
    _SER_TEST_INVOKE(_ser_test_duplicateDecls);
    _SER_TEST_INVOKE(_ser_test_emptyEnum);
    _SER_TEST_INVOKE(_ser_test_emptyEnum2);
    _SER_TEST_INVOKE(_ser_test_unresolvedDeclRef);

    // const char* lknames[] = { "straight", "arc" };
    // ser_specEnum(sk_LineKind, lknames, sizeof(lknames) / sizeof(const char*));
    // ser_specStruct(sk_Line,
    //                kind   sk_LineKind
    //                p1     ptr HMM_Vec2
    //                p2     ptr HMM_Vec2
    //                center ptr HMM_Vec2);
    // ser_specStructOffsets(sk_Line, kind, p1, p2, center);

    // const char* cknames[] = { "distance", "angleLines", "angleArc", "arcUniform", "axisAligned" };
    // ser_specEnum(sk_ConstraintKind, cknames, sizeof(cknames) / sizeof(const char*));
    // ser_specStruct(sk_Constraint,
    //                kind  sk_ConstraintKind
    //                line1 ptr sk_Line
    //                line2 ptr sk_Line
    //                value float);
    // ser_specStructOffsets(sk_Constraint, kind, line1, line2, value);

    // ser_specStruct(sk_Sketch,
    //                points      arr HMM_Vec2
    //                lines       arr sk_Line
    //                constraints arr sk_Constraint);
    // ser_specStructOffsets(sk_Sketch,
    //                       points, pointCount,
    //                       lines, lineCount,
    //                       constraints, constraintCount);
}