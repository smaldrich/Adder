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
    SERE_UNRESOLVED_DECL_TAG,
    SERE_TEST_EXPECT_FAILED,
    SERE_SPECSET_EMPTY,
    SERE_SPECSET_UNLOCKED,
    SERE_SPECSET_LOCKED,
    SERE_DUPLICATE_DECL_TAGS,
    SERE_DUPLICATE_PROP_NAMES,
    SERE_EMPTY_ENUM,
    SERE_EMPTY_TAG,
    SERE_EMPTY_STRUCT_DECL,
    SERE_NO_OFFSETS,
    SERE_WRONG_KIND,
    SERE_PARSE_FAILED,
    SERE_VA_ARG_MISUSE,
    SERE_READ_INST_INITIALIZED,
    SERE_READ_INST_UNINITIALIZED,
} ser_Error;

#define _SER_EXPECT(expr, error) \
    do { \
        if (!(expr)) { \
            return error; \
        } \
    } while (0)

// does not double eval expr :)
// returns the result of expr if expr evaluates to something other than SERE_OK
#define _SER_VALID_OR_RETURN(expr)  \
    do {                            \
        ser_Error _e = expr;        \
        if (_e != SERE_OK) {        \
            return _e;              \
        }                           \
    } while (0)

#define SER_ASSERT(expr) assert(expr)

typedef enum {
    SER_DK_STRUCT,
    SER_DK_ENUM,
} ser_DeclKind;

typedef enum {
    SER_PK_CHAR,
    SER_PK_INT,
    SER_PK_FLOAT,

    // SER_PK_ARRAY_INTERNAL, // an array that fits completely within a struct
    SER_PK_ARRAY_EXTERNAL, // an array allocated outside of the struct
    // SER_PK_PTR,
    _SER_PK_PARSABLE_COUNT,

    SER_PK_DECL_REF, // indicates that the type for this is another declaration (struct or enum)
} ser_PropKind;
// ^^^^VVVV order between these needs to stay consistant for parser to work properly
// decl ref intentionally left out because this list is used for matching parses, and decl ref is selected when nothing here matches
const char* ser_propKindParseNames[] = {
    "char",
    "int",
    "float",
    // "arrIn",
    "arrEx",
    // "ptr"
};

typedef struct ser_Prop ser_Prop;
typedef struct ser_Decl ser_Decl;

struct ser_Prop {
    ser_PropKind kind;
    ser_Prop* nextProp;

    ser_Prop* innerProp; // valid for any non terminal kind - check _ser_isPropKindNonTerminal

    // these two are filled in as soon as the spec is parsed from the user. The pointer needs to wait until
    // the whole set is validated
    const char* declRefTag;
    int64_t declRefTagLen;
    ser_Decl* declRef; // filled in during specset validation

    // location of this member inside of the parent struct, from the start, in bytes, used for reading and
    // writing to structs in the program
    uint64_t parentStructOffset;
    uint64_t arrayLengthParentStructOffset;

    const char* tag;
    uint64_t tagLen;
};

struct ser_Decl {
    ser_DeclKind kind;
    ser_Decl* nextDecl;
    const char* tag;

    uint64_t id; // calcualted on lock, just the index in the specSet list

    union {
        struct {
            ser_Prop* structFirstChild;
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
    uint64_t declCount;

    bool isValidAndLocked;
} ser_SpecSet;

void _ser_clearSpecSet(ser_SpecSet* set) {
    bump_clear(&set->arena);
    set->firstDecl = NULL;
    set->lastDecl = NULL;
    set->isValidAndLocked = false;
    set->declCount = 0;
}

ser_SpecSet _globalSpecSet; // TODO: actually init the damn arena

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SPEC CONSTRUCTION
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

// pushes a new decl, fills in tag and kind, and adds it to the list for the set
ser_Decl* _ser_declPush(ser_SpecSet* set) {
    SER_ASSERT(!set->isValidAndLocked);
    ser_Decl* s = BUMP_PUSH_NEW(&set->arena, ser_Decl);

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

bool _ser_isPropKindNonTerminal(ser_PropKind k) {
    if (k == SER_PK_ARRAY_EXTERNAL) {
        return true;
    }
    return false;
}

ser_Error _ser_tryPatchDeclRef(ser_SpecSet* set, ser_Prop* p) {
    if (p->kind != SER_PK_DECL_REF) {
        if (_ser_isPropKindNonTerminal(p->kind)) {
            return _ser_tryPatchDeclRef(set, p->innerProp);
        }
        return SERE_OK; // in the case where there is a terminal and it wasn't a SER_PK_DECL_REF node, all good
    }

    ser_Decl* decl = _ser_declGetByTag(set, p->declRefTag, p->declRefTagLen);
    if (decl == NULL) {
        return SERE_UNRESOLVED_DECL_TAG;
    }
    p->declRef = decl;

    return SERE_OK;
}

// TODO: test if this works getting called on an already validated/locked specset
ser_Error _ser_validateAndLockSpecSet(ser_SpecSet* set) {
    // fill in IDs of each declaration, collect the count of itesm in the list
    for (ser_Decl* decl = set->firstDecl; decl; decl = decl->nextDecl) {
        decl->id = set->declCount;
        set->declCount++;
        _SER_EXPECT(strlen(decl->tag) > 0, SERE_EMPTY_TAG);

        // make sure this name isn't in conflict with any others
        for (ser_Decl* other = decl->nextDecl; other; other = other->nextDecl) {
            if (strcmp(other->tag, decl->tag) == 0) {
                return SERE_DUPLICATE_DECL_TAGS;
            }
        }
    }
    _SER_EXPECT(set->declCount != 0, SERE_SPECSET_EMPTY);

    // TODO: fail on circular struct composition

    // once IDs are filled in, go back and patch any decl ref pointers
    // TODO: pointers to every ref could be found at spec construction time and this search wouldn't have to happen
    // TODO: inner prop strs shouldn't be null
    for (ser_Decl* decl = set->firstDecl; decl; decl = decl->nextDecl) {
        if (decl->kind == SER_DK_STRUCT) {
            _SER_EXPECT(decl->structSize != 0, SERE_EMPTY_STRUCT_DECL);
            _SER_EXPECT(decl->structOffsetsGiven, SERE_NO_OFFSETS);

            int64_t propCount = 0;
            for (ser_Prop* prop = decl->structFirstChild; prop; prop = prop->nextProp) {
                // link any refs with actual pointers
                // TODO: this is using tags to find and patch RN, might need to change this when specs from files come in
                ser_Error e = _ser_tryPatchDeclRef(set, prop);
                _SER_VALID_OR_RETURN(e);
                propCount++;

                // check duplicates
                for (ser_Prop* other = prop->nextProp; other; other = other->nextProp) {
                    if (other->tagLen == prop->tagLen) {
                        if (strncmp(other->tag, prop->tag, prop->tagLen) == 0) {
                            return SERE_DUPLICATE_PROP_NAMES;
                        }
                    }
                }
            }
            decl->structPropCount = propCount;
        } else if (decl->kind == SER_DK_ENUM) {
            _SER_EXPECT(decl->enumVals != NULL, SERE_EMPTY_ENUM);
            _SER_EXPECT(decl->enumValCount != 0, SERE_EMPTY_ENUM);
        } else {
            return SERE_WRONG_KIND;
        }
    }

    set->isValidAndLocked = true;
    return SERE_OK;
}

// public function used to indicate that every spec is done being constructed.
// name is wierd because the user will never have to deal with >1 specset.
// validates and locks the global set.
void ser_lockSpecs() {
    SER_ASSERT(_globalSpecSet.isValidAndLocked == false);
    SER_ASSERT(_ser_validateAndLockSpecSet(&_globalSpecSet) == SERE_OK);
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

    bool expectingAnotherToken = false;
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
        for (uint64_t i = 0; i < _SER_PK_PARSABLE_COUNT; i++) {
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
    _SER_EXPECT(!_globalSpecSet.isValidAndLocked, SERE_SPECSET_LOCKED);
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
        _SER_VALID_OR_RETURN(_ser_parsePropInners(&c, &propSpec));
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
    decl->structFirstChild = firstProp;
    decl->structPropCount = propCount;
    return SERE_OK;
}

ser_Error _ser_specEnum(const char* tag, const char* strs[], int count) {
    _SER_EXPECT(!_globalSpecSet.isValidAndLocked, SERE_SPECSET_LOCKED);
    ser_Decl* s = _ser_declPush(&_globalSpecSet);
    s->tag = tag;
    s->kind = SER_DK_ENUM;
    s->enumVals = strs;
    s->enumValCount = count;
    return SERE_OK;
}

// TODO: document how this function works
ser_Error _ser_specStructOffsets(const char* tag, int structSize, int argCount, ...) {
    _SER_EXPECT(!_globalSpecSet.isValidAndLocked, SERE_SPECSET_LOCKED);

    va_list args;
    va_start(args, argCount);
    int takenCount = 0;

    ser_Decl* structSpec = _ser_declGetByTag(&_globalSpecSet, tag, strlen(tag));
    _SER_EXPECT(structSpec != NULL, SERE_UNRESOLVED_DECL_TAG);
    _SER_EXPECT(structSpec->kind == SER_DK_STRUCT, SERE_WRONG_KIND);

    structSpec->structSize = structSize;
    structSpec->structOffsetsGiven = true;

    for (ser_Prop* prop = structSpec->structFirstChild; prop; prop = prop->nextProp) {
        int64_t offset = va_arg(args, uint64_t);
        takenCount++;
        _SER_EXPECT(takenCount <= argCount, SERE_VA_ARG_MISUSE);
        prop->parentStructOffset = offset;

        if (prop->kind == SER_PK_ARRAY_EXTERNAL) {
            prop->arrayLengthParentStructOffset = va_arg(args, uint64_t);
            takenCount++;
            _SER_EXPECT(takenCount <= argCount, SERE_VA_ARG_MISUSE);
        }
    }

    va_end(args);
    _SER_EXPECT(takenCount == argCount, SERE_VA_ARG_MISUSE);
    return SERE_OK;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SERIALIZATION
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

// TODO: figure out a way to test desserialization on a big endian system
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
*/

int64_t _ser_sizeOfProp(ser_Prop* p) {
    if (p->kind == SER_PK_CHAR) {
        return sizeof(char);
    } else if (p->kind == SER_PK_INT) {
        return sizeof(int);
    } else if (p->kind == SER_PK_FLOAT) {
        return sizeof(float);
    } else if (p->kind == SER_PK_ARRAY_EXTERNAL) {
        SER_ASSERT(false);
    } else if (p->kind == SER_PK_DECL_REF) {
        // TODO: ENUMS BRO
        SER_ASSERT(p->declRef->kind == SER_DK_STRUCT);
        return p->declRef->structSize;
    } else {
        SER_ASSERT(false);
    }
}

ser_Error _ser_serializeStructByPropSpec(FILE* file, void* obj, ser_Prop* spec);
ser_Error _ser_serializeStructByDeclSpec(FILE* file, void* obj, ser_Decl* spec);

ser_Error _ser_serializeStructByPropSpec(FILE* file, void* obj, ser_Prop* spec) {
    if (spec->kind == SER_PK_FLOAT) {
        _SER_WRITE_OR_FAIL(obj, sizeof(float), file);
    } else if (spec->kind == SER_PK_INT) {
        _SER_WRITE_OR_FAIL(obj, sizeof(int), file);
    } else if (spec->kind == SER_PK_CHAR) {
        _SER_WRITE_OR_FAIL(obj, sizeof(char), file);
    } else if (spec->kind == SER_PK_DECL_REF) {
        return _ser_serializeStructByDeclSpec(file, obj, spec->declRef);
    } else if (spec->kind == SER_PK_ARRAY_EXTERNAL) {
        // TODO: assuming type, will fuck w you later
        uint64_t arrCount = *_SER_LOOKUP_MEMBER(uint64_t, obj, spec->arrayLengthParentStructOffset);
        _SER_WRITE_VAR_OR_FAIL(uint64_t, arrCount, file);

        int64_t innerSize = _ser_sizeOfProp(spec->innerProp); // array of arrays fails here // TODO: is that a bad thing?
        void* arrayPtr = _SER_LOOKUP_MEMBER(void, obj, spec->parentStructOffset);
        for (uint64_t i = 0; i < arrCount; i++) {
            uint64_t offset = (uint64_t)((char*)arrayPtr + (i * innerSize));
            void* ptr = _SER_LOOKUP_MEMBER(void, arrayPtr, offset);
            ser_Error e = _ser_serializeStructByPropSpec(file, ptr, spec->innerProp);
            _SER_VALID_OR_RETURN(e);
        }
    } else {
        SER_ASSERT(false);
    }
    return SERE_OK;
}

ser_Error _ser_serializeStructByDeclSpec(FILE* file, void* obj, ser_Decl* spec) {
    // TODO: ENUMS BRO
    for (ser_Prop* prop = spec->structFirstChild; prop; prop = prop->nextProp) {
        void* propLoc = _SER_LOOKUP_MEMBER(void, obj, prop->parentStructOffset);
        _ser_serializeStructByPropSpec(file, propLoc, prop);
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

ser_Error _ser_writeObjectToFileInner(const char* type, void* obj, FILE* file) {
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
            for (ser_Prop* prop = decl->structFirstChild; prop; prop = prop->nextProp) {
                SER_ASSERT(prop->tagLen != 0);
                _SER_WRITE_VAR_OR_FAIL(uint64_t, prop->tagLen, file);
                _SER_WRITE_OR_FAIL(prop->tag, prop->tagLen, file);
                _ser_serializeProp(file, prop);
            }
        } else {
            SER_ASSERT(false);
        }
    }

    // OBJECTS =============================================================
    ser_Decl* spec = _ser_declGetByTag(&_globalSpecSet, type, strlen(type));
    SER_ASSERT(spec->kind == SER_DK_STRUCT);
    _SER_WRITE_VAR_OR_FAIL(uint64_t, spec->id, file);
    _ser_serializeStructByDeclSpec(file, obj, spec);
    return SERE_OK;
}

// this function got split so that early error returns from the inner still close the file properly
// wish I could have inlined the function but c sucks
ser_Error ser_writeObjectToFile(const char* path, const char* type, void* obj) {
    _SER_EXPECT(_globalSpecSet.isValidAndLocked == true, SERE_SPECSET_UNLOCKED);

    FILE* file = fopen(path, "wb");
    _SER_EXPECT(file != NULL, SERE_FOPEN_FAILED);

    ser_Error e = _ser_writeObjectToFileInner(type, obj, file);
    SER_ASSERT(fclose(file) == 0);
    return e;
}

// TODO: the push system for more than one object per file

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
    bool initialized;
    FILE* file;
    BumpAlloc arena; // TODO: allocator macro so that bump isn't embedded in here

    uint64_t fileSerVersion;
    uint64_t fileVersion;
} ser_ReadInstance;

// allocates tags into the specset arena
ser_Error _ser_dserDecl(ser_SpecSet* set, ser_ReadInstance* inst) {
    _SER_EXPECT(!set->isValidAndLocked, SERE_SPECSET_LOCKED);

    ser_Decl* decl = _ser_declPush(set);

    uint8_t kind = 0;
    _SER_READ_VAR_OR_FAIL(uint8_t, kind, inst->file);
    decl->kind = kind;

    uint64_t strLen = 0;
    _SER_READ_VAR_OR_FAIL(uint64_t, strLen, inst->file);
    char* str = BUMP_PUSH_ARR(&set->arena, strLen + 1, char); // add one for the null term >:(
    _SER_READ_OR_FAIL(str, strLen, inst->file);
    decl->tag = str;

    return SERE_OK;
}

// TODO: file patches! // TODO: can we embed backwards compatibility things too?
ser_Error ser_dserStart(const char* path, ser_ReadInstance* outInst) {
    _SER_EXPECT(outInst->initialized == false, SERE_READ_INST_INITIALIZED);
    memset(outInst, 0, sizeof(ser_ReadInstance));

    outInst->file = fopen(path, "rb");
    _SER_EXPECT(outInst->file != NULL, SERE_FOPEN_FAILED);

    _SER_READ_VAR_OR_FAIL(uint64_t, outInst->fileSerVersion, outInst->file);
    _SER_READ_VAR_OR_FAIL(uint64_t, outInst->fileVersion, outInst->file);

    ser_SpecSet newSet;
    memset(&newSet, 0, sizeof(ser_SpecSet));

    uint64_t declCount = 0;
    _SER_READ_VAR_OR_FAIL(uint64_t, declCount, outInst->file); // TODO: should there be a cap on this?
    for (uint64_t i = 0; i < declCount; i++) {
        _SER_VALID_OR_RETURN(_ser_dserDecl(&newSet, outInst));
    }
    return SERE_OK;
}

// void ser_dserQueryNext() {
// }

// void ser_dserPullNext() {
// }

void ser_dserEnd(ser_ReadInstance* inst) {
    if (inst->file != NULL) {
        fclose(inst->file);
    }
    bump_free(&inst->arena);
    memset(inst, 0, sizeof(*inst));
}

// created on some public functions so that they fail unrecoverably in user code, but not in testing code
#define SER_ASSERT_OK(expr) (SER_ASSERT(expr) == SERE_OK)

#define _SER_STRINGIZE(x) #x
#define ser_specStruct(T, str) SER_ASSERT_OK(_ser_specStruct(_SER_STRINGIZE(T), _SER_STRINGIZE(str)))
#define ser_specEnum(T, strs, count) SER_ASSERT_OK(_ser_specEnum(_SER_STRINGIZE(T), strs, count))

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
        _SER_EXPECT(_got == expected, SERE_TEST_EXPECT_FAILED);                \
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
    _ser_clearSpecSet(&_globalSpecSet);

    _SER_VALID_OR_RETURN(
        ser_specStruct(HMM_Vec2,
                       X float
                       Y float));
    _SER_VALID_OR_RETURN(ser_specStructOffsets(HMM_Vec2, X, Y));

    _SER_VALID_OR_RETURN(_ser_validateAndLockSpecSet(&_globalSpecSet));

    HMM_Vec2 v = HMM_V2(69, 420);
    _SER_VALID_OR_RETURN(ser_writeObjectToFile("./testing/file1", "HMM_Vec2", &v));

    FILE* f = fopen("./testing/file1", "rb");
    _SER_EXPECT(f != NULL, SERE_FOPEN_FAILED);

    _SER_TEST_READ(uint64_t, 0, f); // ser version
    _SER_TEST_READ(uint64_t, 0, f); // app version
    _SER_TEST_READ(uint64_t, 1, f); // spec count

    _SER_TEST_READ(uint8_t, SER_DK_STRUCT, f); // first spec is a struct
    _SER_TEST_READ(uint64_t, 8, f); // length of name str
    _SER_VALID_OR_RETURN(_ser_test_expectString("HMM_Vec2", 8, f)); // name
    _SER_TEST_READ(uint64_t, 2, f); // prop count

    _SER_TEST_READ(uint64_t, 1, f); // tag len
    _SER_VALID_OR_RETURN(_ser_test_expectString("X", 1, f));
    _SER_TEST_READ(uint8_t, SER_PK_FLOAT, f); // kind should be a float

    _SER_TEST_READ(uint64_t, 1, f); // tag len
    _SER_VALID_OR_RETURN(_ser_test_expectString("Y", 1, f));
    _SER_TEST_READ(uint8_t, SER_PK_FLOAT, f); // kind should be a float

    _SER_TEST_READ(uint64_t, 0, f); // origin spec should be index 0, the vector
    _SER_TEST_READ(float, 69, f);
    _SER_TEST_READ(float, 420, f);

    uint8_t eofProbe = 0;
    _SER_EXPECT(fread(&eofProbe, sizeof(uint8_t), 1, f) == 0, SERE_TEST_EXPECT_FAILED);

    fclose(f);
    return SERE_OK;
}

ser_Error _ser_test_shortStructSpec() {
    _ser_clearSpecSet(&_globalSpecSet);
    _SER_EXPECT(
        ser_specStruct(myStruct,
                       X float
                       Y int
                       Z arrEx) == SERE_PARSE_FAILED,
        SERE_TEST_EXPECT_FAILED);
    return SERE_OK;
}

ser_Error _ser_test_badStructOffsetsCall() {
    _ser_clearSpecSet(&_globalSpecSet);
    _SER_VALID_OR_RETURN(ser_specStruct(HMM_Vec2, X float Y float));
    _SER_EXPECT(
        ser_specStructOffsets(HMM_Vec2, X) == SERE_VA_ARG_MISUSE,
        SERE_TEST_EXPECT_FAILED);
    return SERE_OK;
}

ser_Error _ser_test_duplicateStructProps() {
    _ser_clearSpecSet(&_globalSpecSet);
    _SER_VALID_OR_RETURN(
        ser_specStruct(HMM_Vec2,
                       x int
                       x char));
    _SER_VALID_OR_RETURN(ser_specStructOffsets(HMM_Vec2, X, Y));
    _SER_EXPECT(
        _ser_validateAndLockSpecSet(&_globalSpecSet) == SERE_DUPLICATE_PROP_NAMES,
        SERE_TEST_EXPECT_FAILED);
    return SERE_OK;
}

ser_Error _ser_test_duplicateDecls() {
    _ser_clearSpecSet(&_globalSpecSet);
    _SER_VALID_OR_RETURN(ser_specStruct(myStruct, x int));
    const char* vals[] = { "hello" };
    _SER_VALID_OR_RETURN(ser_specEnum(myStruct, vals, 1));
    _SER_EXPECT(
        _ser_validateAndLockSpecSet(&_globalSpecSet) == SERE_DUPLICATE_DECL_TAGS,
        SERE_TEST_EXPECT_FAILED);
    return SERE_OK;
}

ser_Error _ser_test_emptyEnum() {
    _ser_clearSpecSet(&_globalSpecSet);
    const char* vals[] = { "hello" };
    _SER_VALID_OR_RETURN(ser_specEnum(enum, vals, 0));
    _SER_EXPECT(
        _ser_validateAndLockSpecSet(&_globalSpecSet) == SERE_EMPTY_ENUM,
        SERE_TEST_EXPECT_FAILED);
    return SERE_OK;
}

ser_Error _ser_test_emptyEnum2() {
    _ser_clearSpecSet(&_globalSpecSet);
    _SER_VALID_OR_RETURN(ser_specEnum(enum, NULL, 10));
    ser_Error e = _ser_validateAndLockSpecSet(&_globalSpecSet);
    _SER_EXPECT(e == SERE_EMPTY_ENUM, SERE_TEST_EXPECT_FAILED);
    return SERE_OK;
}

ser_Error _ser_test_unresolvedDeclRef() {
    _ser_clearSpecSet(&_globalSpecSet);
    ser_Error e = ser_specStruct(HMM_Vec2,
                                 next struct2
                                 prev struct1);
    _SER_VALID_OR_RETURN(e);
    _SER_VALID_OR_RETURN(ser_specStructOffsets(HMM_Vec2, X, Y));

    e = _ser_validateAndLockSpecSet(&_globalSpecSet);
    _SER_EXPECT(e == SERE_UNRESOLVED_DECL_TAG, SERE_TEST_EXPECT_FAILED);
    return SERE_OK;
}

// print a test with the result of the function being OK and the name being the name of the function
#define _SER_TEST_INVOKE(func) test_printResult(func() == SERE_OK, _SER_STRINGIZE(func))

void ser_tests() {
    test_printSectionHeader("Ser");

    _globalSpecSet.arena = bump_allocate(1000000, "global specset arena");

    _SER_TEST_INVOKE(_ser_test_serializeVec2);
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