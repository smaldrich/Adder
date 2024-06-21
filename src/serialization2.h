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
    SERE_BAD_POINTER,
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
    SERE_READ_INST_INITIALIZED,
    SERE_READ_INST_UNINITIALIZED,
    SERE_SPEC_MISMATCH,
    SERE_INVALID_PULL_TYPE,
    SERE_UNINITIALIZED_WRITER,
} ser_Error;
// TODO: array of readable strings for each error code
// TODO: cull unused errors

// TODO: organize error enum
// TODO: singleheader-ify this
// TODO: docs pass
// TODO: write down possible error returns from each public function

#define _SER_EXPECT(expr, error) \
    do { \
        if (!(expr)) { \
            return error; \
        } \
    } while (0)

// does not double eval expr :)
// returns the result of expr if expr evaluates to something other than SERE_OK
#define _SER_EXPECT_OK(expr)  \
    do {                            \
        ser_Error _e = expr;        \
        if (_e != SERE_OK) {        \
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
} ser_DeclKind;

typedef enum {
    SER_PK_CHAR,
    SER_PK_INT,
    SER_PK_FLOAT,
    // SER_PK_ARRAY_INTERNAL, // an array that fits completely within a struct
    // SER_PK_ARRAY_EXTERNAL, // an array allocated outside of the struct // TODO: the array thing
    // SER_PK_PTR,
    SER_PK_DECL_REF, // indicates that the type for this is another declaration (struct or enum)
    SER_PK_COUNT,
} ser_PropKind;
// ^^^^VVVV order between these needs to stay consistant for parser to work properly
// decl ref intentionally left out because this list is used for matching parses, and decl ref is selected when nothing here matches
const char* ser_propKindParseNames[] = {
    "char",
    "int",
    "float",
    // "arrIn",
    // "arrEx",
    // "ptr"
};

typedef struct ser_Prop ser_Prop;
typedef struct ser_Decl ser_Decl;

// TODO: docs and comment pass

struct ser_Prop {
    ser_PropKind kind;
    ser_Prop* nextProp;

    ser_Prop* innerProp; // valid for any non terminal kind - check _ser_isPropKindNonTerminal

    const char* declRefTag; // used to patch the declref ptr after a specset from the user is validated
    int64_t declRefTagLen;
    uint64_t declRefId; // ID for the ref, used for patching the pointer when specs are loaded from files
    ser_Decl* declRef;

    // location of this member inside of the parent struct, from the start, in bytes, used for reading and
    // writing to structs in the program
    uint64_t parentStructOffset;

    const char* tag;
    uint64_t tagLen;
};

struct ser_Decl {
    ser_DeclKind kind;
    ser_Decl* nextDecl;
    const char* tag;

    // done so that 0 is an invalid index, to make sure that patching references is less error prone
    uint64_t id; // 1 BASED index into the specSet list // filled in on push

    union {
        struct {
            ser_Prop* structFirstProp;
            uint64_t structSize;
            uint64_t structPropCount;
            bool structOffsetsGiven; // flag for if ser_specOffsets has been called on a struct
        };
        struct {
            uint64_t enumValCount;
            const char** enumVals;
        };
    };
};

typedef struct {
    BumpAlloc arena; // TODO: don't embed these as a dep

    ser_Decl* firstDecl;
    ser_Decl* lastDecl;
    uint64_t declCount; // calcualted with _ser_declPush, but double checked at validation time
} ser_SpecSet;

void _ser_clearSpecSet(ser_SpecSet* set) {
    bump_clear(&set->arena);
    set->firstDecl = NULL;
    set->lastDecl = NULL;
    set->declCount = 0;
}

ser_SpecSet _globalSpecSet;
bool _isGlobalSetLocked;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SPEC CONSTRUCTION
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

ser_SpecSet _ser_specSetInit(const char* arenaName) {
    ser_SpecSet out;
    memset(&out, 0, sizeof(out));
    out.arena = bump_allocate(1000000, arenaName); // TODO: should this be able to grow tho????
    return out;
}

// pushes a new decl, fills the ID as the index, and pushes it to the sets list
ser_Decl* _ser_declPush(ser_SpecSet* set) {
    ser_Decl* s = BUMP_PUSH_NEW(&set->arena, ser_Decl);
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
ser_Decl* _ser_declGetByTag(ser_SpecSet* set, const char* tag, uint64_t tagLen) {
    for (ser_Decl* s = set->firstDecl; s; s = s->nextDecl) {
        if (tagLen == strlen(s->tag)) {
            if (strncmp(tag, s->tag, tagLen) == 0) { // TODO: fuck you stl
                return s;
            }
        }
    }
    return NULL;
}

ser_Decl* _ser_declGetByID(ser_SpecSet* set, uint64_t id) {
    // RN just braindead loop until we find the thing w/ the right ID
    for (ser_Decl* decl = set->firstDecl; decl; decl = decl->nextDecl) {
        if (decl->id == id) {
            return decl;
        }
    }
    return NULL;
}

bool _ser_isPropKindNonTerminal(ser_PropKind k) {
    if (k == 100000000) { // getting rid of compile warning while working w/ no non terminals
        return false;
    }
    // if (k == SER_PK_ARRAY_EXTERNAL) {
    //     return true;
    // }
    return false;
}

// DOES NOT CHECK FOR VALID DECL REFS OR OFFSETS OR MODIFY THE SET IN ANY WAY
ser_Error _ser_checkSpecSetDuplicatesCountsKindsAndEmpties(const ser_SpecSet* set) {
    uint64_t countedDecls = 0; // doing this is very redundant but who cares
    for (ser_Decl* decl = set->firstDecl; decl; decl = decl->nextDecl) {
        for (ser_Decl* other = decl->nextDecl; other; other = other->nextDecl) {
            if (strcmp(other->tag, decl->tag) == 0) {
                return SERE_DUPLICATE_DECL_TAGS;
            }
        }
        countedDecls++;

        if (decl->kind == SER_DK_STRUCT) {
            uint64_t countedProps = 0; // this is also very redundant
            for (ser_Prop* prop = decl->structFirstProp; prop; prop = prop->nextProp) {
                _SER_EXPECT(prop->tagLen > 0, SERE_EMPTY_TAG);
                _SER_EXPECT(prop->tag != NULL, SERE_EMPTY_TAG);
                _SER_EXPECT(prop->kind >= 0 && prop->kind < SER_PK_COUNT, SERE_UNEXPECTED_KIND);

                // check duplicates
                for (ser_Prop* other = prop->nextProp; other; other = other->nextProp) {
                    if (other->tagLen == prop->tagLen) {
                        if (strncmp(other->tag, prop->tag, prop->tagLen) == 0) {
                            return SERE_DUPLICATE_PROP_TAGS;
                        }
                    }
                }
                countedProps++;
            }
            _SER_EXPECT(countedProps != 0, SERE_EMPTY_STRUCT_DECL);
            _SER_EXPECT(countedProps == decl->structPropCount, SERE_SPECSET_INCORRECT_PROP_COUNT);
        } else if (decl->kind == SER_DK_ENUM) {
            _SER_EXPECT(decl->enumVals != NULL, SERE_EMPTY_ENUM);
            _SER_EXPECT(decl->enumValCount != 0, SERE_EMPTY_ENUM);
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
ser_Error _ser_tryPatchDeclRef(ser_SpecSet* set, ser_Prop* p) {
    if (p->kind != SER_PK_DECL_REF) {
        if (_ser_isPropKindNonTerminal(p->kind)) {
            return _ser_tryPatchDeclRef(set, p->innerProp);
        }
        return SERE_OK; // in the case where there is a terminal and it wasn't a SER_PK_DECL_REF node, exit without doing anything
    }

    if (p->declRefTagLen > 0) {
        ser_Decl* decl = _ser_declGetByTag(set, p->declRefTag, p->declRefTagLen);
        if (decl == NULL) {
            return SERE_INVALID_DECL_REF_TAG;
        }
        p->declRef = decl;
        return SERE_OK;
    } else {
        _SER_EXPECT(p->declRefId > 0, SERE_INVALID_DECL_REF_ID);
        _SER_EXPECT(p->declRefId <= set->declCount, SERE_INVALID_DECL_REF_ID); // because 1 indexed, the last ID can be equal to the count
        ser_Decl* decl = _ser_declGetByID(set, p->declRefId);
        if (decl != NULL) {
            p->declRef = decl;
        }
    }
    SER_ASSERT(false);
}

// uses IDs or tags, whichever is present to fill in the declRef ptr in each declRef prop.
// Assumes that _ser_checkSpecSetDuplicatesKindsAndEmpties has been called on the set and was OK.
ser_Error _ser_patchAndCheckSpecSetDeclRefs(ser_SpecSet* set) {
    // TODO: fail on circular struct composition
    for (ser_Decl* decl = set->firstDecl; decl; decl = decl->nextDecl) {
        if (decl->kind == SER_DK_STRUCT) {
            for (ser_Prop* prop = decl->structFirstProp; prop; prop = prop->nextProp) {
                ser_Error e = _ser_tryPatchDeclRef(set, prop);
                _SER_EXPECT_OK(e);
            }
        }
    }
    return SERE_OK;
}

// public function used to indicate that every spec is done being constructed.
// name is wierd because the user will never have to deal with >1 specset.
// validates and locks the global set // should only be called once in user code
// only locks when checks successful
ser_Error _ser_lockSpecs() {
    _SER_EXPECT(_isGlobalSetLocked == false, SERE_GLOBAL_SET_LOCKED);
    _SER_EXPECT_OK(_ser_checkSpecSetDuplicatesCountsKindsAndEmpties(&_globalSpecSet));
    _SER_EXPECT_OK(_ser_patchAndCheckSpecSetDeclRefs(&_globalSpecSet));
    _isGlobalSetLocked = true;
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
// takes the string and parses out a list of specs for *one* prop
// this takes care of arr and ptr needing more tokens after
ser_Error _ser_parsePropInners(const char** str, ser_Prop** outProp) {
    ser_Prop* firstInner = NULL;
    ser_Prop* lastInner = NULL;

    bool expectingAnotherToken = true; // start expecting at least one thing to be present
    while (true) {
        const char* kindStr;
        uint64_t kindStrLen;
        bool newToken = _ser_parseToken(str, &kindStr, &kindStrLen);
        if (!newToken) {
            // non terminals require another token after them, if there are none left but we are expecting one, fail
            if (expectingAnotherToken) {
                return SERE_PARSE_FAILED;
            }
            break;
        }

        ser_PropKind k = SER_PK_DECL_REF; // default to a decl kind if no keywords match
        const uint64_t parsableCount = sizeof(ser_propKindParseNames) / sizeof(const char*);
        for (uint64_t i = 0; i < parsableCount; i++) {
            const char* keywordCandidate = ser_propKindParseNames[i];
            if (strlen(keywordCandidate) == kindStrLen) {
                if (strncmp(ser_propKindParseNames[i], kindStr, kindStrLen) == 0) {
                    k = i;
                    break;
                }
            }
        } // end matching keywords for prop kinds

        ser_Prop* inner = BUMP_PUSH_NEW(&_globalSpecSet.arena, ser_Prop);
        inner->kind = k;

        if (inner->kind == SER_PK_DECL_REF) {
            inner->declRefTag = kindStr;
            inner->declRefTagLen = kindStrLen;
        }

        if (!firstInner) {
            firstInner = inner;
            lastInner = inner;
        } else {
            // push to the back of the list
            lastInner->innerProp = inner;
            lastInner = inner;
        }

        if (_ser_isPropKindNonTerminal(inner->kind)) {
            expectingAnotherToken = true;
        } else {
            break; // reached a terminal, stop looking for more inners
        }
    }
    *outProp = firstInner;
    return SERE_OK;
}

ser_Error _ser_specStruct(const char* tag, const char* str) {
    _SER_EXPECT(!_isGlobalSetLocked, SERE_GLOBAL_SET_LOCKED);

    ser_Decl* decl = _ser_declPush(&_globalSpecSet);
    decl->tag = tag;
    decl->kind = SER_DK_STRUCT;

    ser_Prop* firstProp = NULL;
    ser_Prop* lastProp = NULL;
    int64_t propCount = 0;

    const char* c = str;
    while (true) {
        const char* propName = NULL;
        uint64_t propNameLen = 0;
        bool newToken = _ser_parseToken(&c, &propName, &propNameLen);
        if (!newToken) {
            break;
        }
        ser_Prop* propSpec = NULL;
        _SER_EXPECT_OK(_ser_parsePropInners(&c, &propSpec));
        propSpec->tag = propName;
        propSpec->tagLen = propNameLen;

        if (firstProp == NULL) {
            firstProp = propSpec;
            lastProp = propSpec;
        } else {
            lastProp->nextProp = propSpec;
            lastProp = propSpec;
        }
        propCount++;
    }

    _SER_EXPECT(firstProp != NULL, SERE_EMPTY_STRUCT_DECL);
    decl->structFirstProp = firstProp;
    decl->structPropCount = propCount;
    return SERE_OK;
}

ser_Error _ser_specEnum(const char* tag, const char* strs[], int count) {
    _SER_EXPECT(!_isGlobalSetLocked, SERE_GLOBAL_SET_LOCKED);
    ser_Decl* s = _ser_declPush(&_globalSpecSet);
    s->tag = tag;
    s->kind = SER_DK_ENUM;
    s->enumVals = strs;
    s->enumValCount = count;
    return SERE_OK;
}

// TODO: document how this function works
ser_Error _ser_specStructOffsets(const char* tag, int structSize, int argCount, ...) {
    _SER_EXPECT(!_isGlobalSetLocked, SERE_GLOBAL_SET_LOCKED);
    va_list args;
    va_start(args, argCount);
    int takenCount = 0;

    ser_Decl* structSpec = _ser_declGetByTag(&_globalSpecSet, tag, strlen(tag));
    _SER_EXPECT(structSpec != NULL, SERE_INVALID_DECL_REF_TAG);
    _SER_EXPECT(structSpec->kind == SER_DK_STRUCT, SERE_UNEXPECTED_KIND);

    structSpec->structSize = structSize;
    structSpec->structOffsetsGiven = true;

    for (ser_Prop* prop = structSpec->structFirstProp; prop; prop = prop->nextProp) {
        int64_t offset = va_arg(args, uint64_t);
        takenCount++;
        _SER_EXPECT(takenCount <= argCount, SERE_VA_ARG_MISUSE);
        prop->parentStructOffset = offset;

        // if (prop->kind == SER_PK_ARRAY_EXTERNAL) {
        //     prop->arrayLengthParentStructOffset = va_arg(args, uint64_t);
        //     takenCount++;
        //     _SER_EXPECT(takenCount <= argCount, SERE_VA_ARG_MISUSE);
        // }
    }

    va_end(args);
    _SER_EXPECT(takenCount == argCount, SERE_VA_ARG_MISUSE);
    return SERE_OK;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SERIALIZATION
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

// TODO: figure out a way to test deserialization on a big endian system
bool _ser_isSystemLittleEndian() {
    uint32_t x = 1;
    return *(char*)(&x) == 1;
}

#define _SER_LOOKUP_MEMBER(outT, obj, offset) ((outT*)(((char*)obj) + (offset)))

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

uint64_t object count
    uint64_t spec ID (index)
        parse by prop order dictated in the spec :)
        where arrays are a uint64_t for count and then repeated inner elements
        where enums are just int32_ts w the index of the value
*/

int64_t _ser_sizeOfProp(ser_Prop* p) {
    if (p->kind == SER_PK_CHAR) {
        return sizeof(char);
    } else if (p->kind == SER_PK_INT) {
        return sizeof(int);
    } else if (p->kind == SER_PK_FLOAT) {
        return sizeof(float);
        // } else if (p->kind == SER_PK_ARRAY_EXTERNAL) {
        //     SER_ASSERT(false);
    } else if (p->kind == SER_PK_DECL_REF) {
        if (p->declRef->kind == SER_DK_STRUCT) {
            return p->declRef->structSize;
        } else if (p->declRef->kind == SER_DK_ENUM) {
            return sizeof(int32_t); // TODO: assuming size, this will fuck someone over badly eventually
        } else {
            SER_ASSERT(false);
        }
    } else {
        SER_ASSERT(false);
    }
}

ser_Error _ser_serializeObjByPropSpec(FILE* file, void* obj, ser_Prop* spec);
ser_Error _ser_serializeObjByDeclSpec(FILE* file, void* obj, ser_Decl* spec);

ser_Error _ser_serializeObjByPropSpec(FILE* file, void* obj, ser_Prop* spec) {
    if (spec->kind == SER_PK_FLOAT) {
        _SER_WRITE_OR_FAIL(obj, sizeof(float), file);
    } else if (spec->kind == SER_PK_INT) {
        _SER_WRITE_OR_FAIL(obj, sizeof(int), file);
    } else if (spec->kind == SER_PK_CHAR) {
        _SER_WRITE_OR_FAIL(obj, sizeof(char), file);
    } else if (spec->kind == SER_PK_DECL_REF) {
        return _ser_serializeObjByDeclSpec(file, obj, spec->declRef);
        // } else if (spec->kind == SER_PK_ARRAY_EXTERNAL) {
        //     // TODO: assuming type, will fuck w you later
        //     uint64_t arrCount = *_SER_LOOKUP_MEMBER(uint64_t, obj, spec->arrayLengthParentStructOffset);
        //     _SER_WRITE_VAR_OR_FAIL(uint64_t, arrCount, file);

        //     int64_t innerSize = _ser_sizeOfProp(spec->innerProp); // array of arrays fails here // TODO: is that a bad thing?
        //     void* arrayPtr = _SER_LOOKUP_MEMBER(void, obj, spec->parentStructOffset);
        //     for (uint64_t i = 0; i < arrCount; i++) {
        //         uint64_t offset = (uint64_t)((char*)arrayPtr + (i * innerSize));
        //         void* ptr = _SER_LOOKUP_MEMBER(void, arrayPtr, offset);
        //         ser_Error e = _ser_serializeObjByPropSpec(file, ptr, spec->innerProp);
        //         _SER_EXPECT_OK(e);
        //     }
    } else {
        SER_ASSERT(false);
    }
    return SERE_OK;
}

// TODO: enums are assuming adjacent, incrementing from zero values - this isn't correct in some cases
// should be documented or fixed

ser_Error _ser_serializeObjByDeclSpec(FILE* file, void* obj, ser_Decl* spec) {
    if (spec->kind == SER_DK_STRUCT) {
        for (ser_Prop* prop = spec->structFirstProp; prop; prop = prop->nextProp) {
            void* propLoc = _SER_LOOKUP_MEMBER(void, obj, prop->parentStructOffset);
            _ser_serializeObjByPropSpec(file, propLoc, prop);
        }
    } else if (spec->kind == SER_DK_ENUM) {
        _SER_WRITE_OR_FAIL(obj, sizeof(int32_t), file);
    } else {
        SER_ASSERT(false);
    }
    return SERE_OK;
}

ser_Error _ser_serializeProp(FILE* file, ser_Prop* prop) {
    SER_ASSERT(prop->kind < 255 && prop->kind >= 0);
    _SER_WRITE_VAR_OR_FAIL(uint8_t, prop->kind, file);

    if (prop->kind == SER_PK_DECL_REF) {
        _SER_WRITE_VAR_OR_FAIL(uint64_t, prop->declRef->id, file);
    }

    if (_ser_isPropKindNonTerminal(prop->kind)) {
        _ser_serializeProp(file, prop->innerProp);
    }
    return SERE_OK;
}

// TODO: remove as many asserts as possible

// removed from writer init because the early returns
ser_Error _ser_serializeGlobalSpec(FILE* file) {
    _SER_WRITE_VAR_OR_FAIL(uint64_t, 0, file); // ser version indicator
    _SER_WRITE_VAR_OR_FAIL(uint64_t, 0, file); // app version indicator
    _SER_WRITE_VAR_OR_FAIL(uint64_t, _globalSpecSet.declCount, file);

    for (ser_Decl* decl = _globalSpecSet.firstDecl; decl; decl = decl->nextDecl) {
        SER_ASSERT(decl->kind < 255 && decl->kind >= 0);
        _SER_WRITE_VAR_OR_FAIL(uint8_t, decl->kind, file);

        uint64_t tagLen = strlen(decl->tag);
        _SER_WRITE_VAR_OR_FAIL(uint64_t, tagLen, file);
        _SER_WRITE_OR_FAIL(decl->tag, tagLen, file);

        if (decl->kind == SER_DK_ENUM) {
            _SER_WRITE_VAR_OR_FAIL(uint64_t, decl->enumValCount, file);
            for (uint64_t enumValIdx = 0; enumValIdx < decl->enumValCount; enumValIdx++) {
                const char* val = decl->enumVals[enumValIdx];
                uint64_t valStrLen = strlen(val);
                _SER_WRITE_VAR_OR_FAIL(uint64_t, valStrLen, file);
                _SER_WRITE_OR_FAIL(val, valStrLen, file);
            }
        } else if (decl->kind == SER_DK_STRUCT) {
            _SER_WRITE_VAR_OR_FAIL(uint64_t, decl->structPropCount, file);
            for (ser_Prop* prop = decl->structFirstProp; prop; prop = prop->nextProp) {
                SER_ASSERT(prop->tagLen != 0);
                _SER_WRITE_VAR_OR_FAIL(uint64_t, prop->tagLen, file);
                _SER_WRITE_OR_FAIL(prop->tag, prop->tagLen, file);
                _ser_serializeProp(file, prop);
            }
        } else {
            SER_ASSERT(false);
        }
    }
    return SERE_OK;
}

typedef struct _ser_WriteNode _ser_WriteNode;
struct _ser_WriteNode {
    void* obj;
    ser_Decl* spec;
    _ser_WriteNode* next;
};

typedef struct {
    bool initialized;
    BumpAlloc arena;
    _ser_WriteNode* firstNode;
    _ser_WriteNode* lastNode;
    FILE* file;
} _ser_Writer;
// TODO: opaque handle

_ser_Writer ser_writerInit(const char* path, ser_Error* outError) {
    *outError = SERE_OK;
    _ser_Writer out;

    if (!_isGlobalSetLocked) {
        *outError = SERE_GLOBAL_SET_UNLOCKED;
        return out;
    }
    memset(&out, 0, sizeof(out));
    out.initialized = true;
    out.arena = bump_allocate(1000000, "ser writer arena");

    out.file = fopen(path, "wb");
    if (!out.file) {
        *outError = SERE_FOPEN_FAILED;
        return out;
    }
    ser_Error e = _ser_serializeGlobalSpec(out.file);
    if (e != SERE_OK) {
        *outError = e;
        return out;
    }
    return out;
    // TODO: do we want asserts here or not
}

ser_Error ser_writerPush(_ser_Writer* writer, void* object, const char* specTag) {
    _SER_EXPECT(writer->initialized, SERE_UNINITIALIZED_WRITER);
    _ser_WriteNode* node = BUMP_PUSH_NEW(&writer->arena, _ser_WriteNode);
    node->obj = object;
    node->spec = _ser_declGetByTag(&_globalSpecSet, specTag, strlen(specTag));
    _SER_EXPECT(node->spec != NULL, SERE_INVALID_DECL_REF_TAG);
    if (writer->firstNode == NULL) {
        writer->firstNode = node;
        writer->lastNode = node;
    } else {
        writer->lastNode->next = node;
        writer->lastNode = node;
    }
    return SERE_OK;
}

// split from writerEnd so that errors still close the file
ser_Error _ser_writeObjects(_ser_Writer* writer) {
    // TODO: infinite loop guards????
    for (_ser_WriteNode* node = writer->firstNode; node; node = node->next) {
        _SER_WRITE_VAR_OR_FAIL(uint64_t, node->spec->id, writer->file);
        _SER_EXPECT_OK(_ser_serializeObjByDeclSpec(writer->file, node->obj, node->spec));
    }
    return SERE_OK;
}

// writes objects, no matter what the file gets closed and nothing leaks, but errors are still passed up
ser_Error ser_writerEnd(_ser_Writer* writer) {
    ser_Error e = _ser_writeObjects(writer);
    bump_free(&writer->arena);
    SER_ASSERT(fclose(writer->file) == 0);
    memset(writer, 0, sizeof(*writer));
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
    ser_SpecSet specSet;

    uint64_t fileSerVersion;
    uint64_t fileVersion;
} ser_DserInstance;

// this function allocates and fills in outProp with a kind and any inner props // also parses out decl_ref id - not tag or ptr
// tags and props allocated to the specsets arena
ser_Error _ser_dserPropInner(ser_DserInstance* inst, ser_Prop** outProp) {
    ser_Prop* prop = BUMP_PUSH_NEW(&inst->specSet.arena, ser_Prop);
    uint8_t kind = 0;
    _SER_READ_VAR_OR_FAIL(uint8_t, kind, inst->file);
    _SER_EXPECT(kind < SER_PK_COUNT, SERE_UNEXPECTED_KIND);
    prop->kind = kind;

    if (_ser_isPropKindNonTerminal(prop->kind)) {
        ser_Prop* inner = NULL;
        _SER_EXPECT_OK(_ser_dserPropInner(inst, &inner));
        SER_ASSERT(inner != NULL);
        prop->innerProp = inner;
    } else if (prop->kind == SER_PK_DECL_REF) {
        _SER_READ_VAR_OR_FAIL(uint64_t, prop->declRefId, inst->file);
    }

    *outProp = prop;
    return SERE_OK;
}

// allocates tags into the specset arena
// moves the inst filestream forwards
ser_Error _ser_dserDecl(ser_DserInstance* inst) {
    ser_Decl* decl = _ser_declPush(&inst->specSet);

    uint8_t kind = 0;
    _SER_READ_VAR_OR_FAIL(uint8_t, kind, inst->file);
    decl->kind = kind;

    uint64_t strLen = 0;
    _SER_READ_VAR_OR_FAIL(uint64_t, strLen, inst->file);
    char* str = BUMP_PUSH_ARR(&inst->specSet.arena, strLen + 1, char); // add one for the null term >:(
    _SER_READ_OR_FAIL(str, strLen, inst->file);
    decl->tag = str;

    if (decl->kind == SER_DK_STRUCT) {
        _SER_READ_VAR_OR_FAIL(uint64_t, decl->structPropCount, inst->file);
        ser_Prop* lastProp = NULL;

        for (uint64_t i = 0; i < decl->structPropCount; i++) {
            uint64_t propTagLen = 0;
            _SER_READ_VAR_OR_FAIL(uint64_t, propTagLen, inst->file);
            char* propTag = BUMP_PUSH_ARR(&inst->specSet.arena, propTagLen + 1, char); // add one for the null term >:(
            _SER_READ_OR_FAIL(propTag, propTagLen, inst->file);

            ser_Prop* prop = NULL;
            _SER_EXPECT_OK(_ser_dserPropInner(inst, &prop));
            prop->tag = propTag;
            prop->tagLen = propTagLen;

            // push to the back of the prop list
            if (!lastProp) {
                decl->structFirstProp = prop;
                lastProp = prop;
            } else {
                lastProp->nextProp = prop;
                lastProp = prop;
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
ser_Error _ser_readToObjFromDecl(void* obj, ser_Decl* decl, FILE* file);
ser_Error _ser_readToObjFromProp(void* obj, ser_Prop* prop, FILE* file);

ser_Error _ser_readToObjFromDecl(void* obj, ser_Decl* decl, FILE* file) {
    if (decl->kind == SER_DK_STRUCT) {
        for (ser_Prop* prop = decl->structFirstProp; prop; prop = prop->nextProp) {
            void* propLoc = _SER_LOOKUP_MEMBER(void, obj, prop->parentStructOffset);
            _SER_EXPECT_OK(_ser_readToObjFromProp(propLoc, prop, file));
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

ser_Error _ser_readToObjFromProp(void* obj, ser_Prop* prop, FILE* file) {
    if (prop->kind == SER_PK_CHAR) {
        _SER_READ_OR_FAIL(obj, sizeof(uint8_t), file);
    } else if (prop->kind == SER_PK_INT) {
        _SER_READ_OR_FAIL(obj, sizeof(int), file);
    } else if (prop->kind == SER_PK_FLOAT) {
        _SER_READ_OR_FAIL(obj, sizeof(float), file);
    } else if (prop->kind == SER_PK_DECL_REF) {
        _SER_EXPECT_OK(_ser_readToObjFromDecl(obj, prop->declRef, file));
    } else {
        SER_ASSERT(false);
    }
    return SERE_OK;
    // TODO: i don't like how many switches there are on every prop kind. its gonna be a bug at some point missing updating one
}



// TODO: file patches! // TODO: can we embed backwards compatibility things too?
ser_Error ser_readObjFromFile(const char* path, const char* type, void* obj) {
    _SER_EXPECT(_isGlobalSetLocked, SERE_GLOBAL_SET_UNLOCKED);

    ser_DserInstance inst;
    // intiailize things, deserialize the spec from the file
    {
        memset(&inst, 0, sizeof(ser_DserInstance));
        inst.specSet = _ser_specSetInit("dser inst spec set arena");
        inst.file = fopen(path, "rb");
        _SER_EXPECT(inst.file != NULL, SERE_FOPEN_FAILED);

        _SER_READ_VAR_OR_FAIL(uint64_t, inst.fileSerVersion, inst.file);
        _SER_READ_VAR_OR_FAIL(uint64_t, inst.fileVersion, inst.file);

        uint64_t declCount = 0;
        // TODO: REASONABLE LIMITS FOR LENGTHS ON DESERIALIZED INFO??
        _SER_READ_VAR_OR_FAIL(uint64_t, declCount, inst.file);
        for (uint64_t i = 0; i < declCount; i++) {
            _SER_EXPECT_OK(_ser_dserDecl(&inst));
        }

        _SER_EXPECT_OK(_ser_checkSpecSetDuplicatesCountsKindsAndEmpties(&_globalSpecSet));
        _SER_EXPECT_OK(_ser_patchAndCheckSpecSetDeclRefs(&_globalSpecSet));
    }

    // TODO: rn checking that current and old specs exactly match, then copying over offsets, add more flex
    // decl reordering, enum reordering, prop reordering, removed props, added props, etc.
    {
        ser_Decl* globalDecl = _globalSpecSet.firstDecl;
        ser_Decl* fileDecl = inst.specSet.firstDecl;
        while (true) {
            // same tags
            _SER_EXPECT(strcmp(globalDecl->tag, fileDecl->tag) == 0, SERE_SPEC_MISMATCH);
            _SER_EXPECT(globalDecl->kind == fileDecl->kind, SERE_SPEC_MISMATCH);

            if (globalDecl->kind == SER_DK_STRUCT) {
                ser_Prop* globalProp = globalDecl->structFirstProp;
                ser_Prop* fileProp = fileDecl->structFirstProp;
                while (true) {
                    _SER_EXPECT(globalProp->tagLen == fileProp->tagLen, SERE_SPEC_MISMATCH);
                    _SER_EXPECT(strncmp(globalProp->tag, fileProp->tag, globalProp->tagLen) == 0, SERE_SPEC_MISMATCH);
                    _SER_EXPECT(globalProp->kind == fileProp->kind, SERE_SPEC_MISMATCH);
                    // TODO: nonterminals are not completely typechecked here bc im lazy af
                    if (_ser_isPropKindNonTerminal(globalProp->kind)) {
                        SER_ASSERT(false);
                    }
                    fileProp->parentStructOffset = globalProp->parentStructOffset;

                    globalProp = globalProp->nextProp;
                    fileProp = fileProp->nextProp;
                    if (fileProp == NULL || globalProp == NULL) {
                        _SER_EXPECT(fileProp == NULL && globalProp == NULL, SERE_SPEC_MISMATCH);
                        break;
                    }
                }
            } else if (globalDecl->kind == SER_DK_ENUM) {
                _SER_EXPECT(globalDecl->enumValCount == fileDecl->enumValCount, SERE_SPEC_MISMATCH);
                for (uint64_t i = 0; i < globalDecl->enumValCount; i++) {
                    _SER_EXPECT(strcmp(globalDecl->enumVals[i], fileDecl->enumVals[i]) == 0, SERE_SPEC_MISMATCH);
                }
            } else {
                SER_ASSERT(false);
            }

            globalDecl = globalDecl->nextDecl;
            fileDecl = fileDecl->nextDecl;
            if (globalDecl == NULL || fileDecl == NULL) {
                _SER_EXPECT(globalDecl == NULL && fileDecl == NULL, SERE_SPEC_MISMATCH);
                break;
            }
        }
    }

    // now that the spec from the file has been filled in properly to dserialize data into structs, do it
    {
        ser_Decl* targetSpec = _ser_declGetByTag(&inst.specSet, type, strlen(type));
        uint64_t targetSpecID = 0;
        _SER_READ_VAR_OR_FAIL(uint64_t, targetSpecID, inst.file);
        _SER_EXPECT(targetSpecID == targetSpec->id, SERE_INVALID_PULL_TYPE);
        SER_ASSERT(targetSpec->kind == SER_DK_STRUCT); // TODO: is this correct?
        _SER_EXPECT_OK(_ser_readToObjFromDecl(obj, targetSpec, inst.file));
    }

    // TODO: this reasource leaks on errors rn, probably should fix that
    if (inst.file != NULL) {
        fclose(inst.file);
    }
    bump_free(&inst.specSet.arena);
    return SERE_OK;
}

#define _SER_STRINGIZE(x) #x
#define ser_specStruct(T, str) SER_ASSERT_OK(_ser_specStruct(_SER_STRINGIZE(T), _SER_STRINGIZE(str)))
#define ser_specEnum(T, strs, count) SER_ASSERT_OK(_ser_specEnum(_SER_STRINGIZE(T), strs, count))
#define ser_lockSpecs() SER_ASSERT_OK(_ser_lockSpecs())

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
    _SER_EXPECT_OK(ser_writeObjectToFile("./testing/serializeVec2", "HMM_Vec2", &v));

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

ser_Error _ser_test_vec2RoundTrip() {
    _SER_EXPECT_OK(
        ser_specStruct(HMM_Vec2,
                       X float
                       Y float));
    _SER_EXPECT_OK(ser_specStructOffsets(HMM_Vec2, X, Y));
    _SER_EXPECT_OK(ser_lockSpecs());

    HMM_Vec2 v = HMM_V2(69, 420);
    _SER_EXPECT_OK(ser_writeObjectToFile("./testing/v2RoundTrip", "HMM_Vec2", &v));

    HMM_Vec2 out = HMM_V2(0, 0);
    _SER_EXPECT_OK(ser_readObjFromFile("./testing/v2RoundTrip", "HMM_Vec2", &out));
    _SER_EXPECT(out.X == v.X && out.Y == v.Y, SERE_TEST_EXPECT_FAILED);
    return SERE_OK;
}

ser_Error _ser_test_serializeEnum() {
    const char* enumVals[] = {
        "first",
        "second",
        "third",
    };
    _SER_EXPECT_OK(ser_specEnum(myEnum, enumVals, 3));
    _SER_EXPECT_OK(ser_lockSpecs());
    int32_t enumVal = 1;
    _SER_EXPECT_OK(ser_writeObjectToFile("./testing/serializeEnum", "myEnum", &enumVal));

    FILE* f = fopen("./testing/serializeEnum", "rb");
    _SER_EXPECT(f != NULL, SERE_FOPEN_FAILED);

    _SER_TEST_READ(uint64_t, 0, f); // ser version
    _SER_TEST_READ(uint64_t, 0, f); // app version
    _SER_TEST_READ(uint64_t, 1, f); // spec count

    _SER_TEST_READ(uint8_t, SER_DK_ENUM, f); // decl kind
    _SER_TEST_READ(uint64_t, 6, f); // length of tag string
    _SER_EXPECT_OK(_ser_test_expectString("myEnum", 6, f));

    _SER_TEST_READ(uint64_t, 3, f); // number of values
    _SER_TEST_READ(uint64_t, 5, f); // length of first string
    _SER_EXPECT_OK(_ser_test_expectString("first", 5, f));
    _SER_TEST_READ(uint64_t, 6, f);
    _SER_EXPECT_OK(_ser_test_expectString("second", 6, f));
    _SER_TEST_READ(uint64_t, 5, f);
    _SER_EXPECT_OK(_ser_test_expectString("third", 5, f));

    _SER_TEST_READ(uint64_t, 1, f); // first obj ID should be the enum
    _SER_TEST_READ(int32_t, 1, f); // value
    return SERE_OK;
}

ser_Error _ser_test_shortStructSpec() {
    _SER_EXPECT(
        ser_specStruct(myStruct,
                       X float
                       Y int
                       Z) == SERE_PARSE_FAILED,
        SERE_TEST_EXPECT_FAILED);
    // TODO: make this but with a non terminal
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
    _ser_clearSpecSet(&_globalSpecSet);
    _isGlobalSetLocked = false;

    ser_Error e = func();
    test_printResult(e == SERE_OK, name);
    if (e != SERE_OK) {
        printf("\tError code: %d\n", e);
    }
}

void ser_tests() {
    test_printSectionHeader("Ser");

    _globalSpecSet = _ser_specSetInit("global spec set arena");
    _SER_TEST_INVOKE(_ser_test_serializeVec2);
    _SER_TEST_INVOKE(_ser_test_vec2RoundTrip);
    _SER_TEST_INVOKE(_ser_test_serializeEnum);
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