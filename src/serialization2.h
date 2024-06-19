#pragma once
#include <inttypes.h>
#include <memory.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>

typedef enum {
    SERE_OK,
    SERE_FOPEN_FAILED,
    SERE_FWRITE_FAILED,
    SERE_FREAD_FAILED,
    SERE_BAD_POINTER,
    SERE_UNRESOLVED_USER_SPEC_TAG,
    SERE_TEST_EXPECT_FAILED,
    SERE_SPECSET_EMPTY,
    SERE_SPECSET_UNLOCKED,
    SERE_DUPLICATE_USER_SPEC_NAMES,
    SERE_DUPLICATE_PROP_NAMES,
    SERE_EMPTY_ENUM,
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

typedef enum {
    SER_SU_STRUCT,
    SER_SU_ENUM,
} ser_SpecUserKind;

typedef enum {
    SER_SP_CHAR,
    SER_SP_INT,
    SER_SP_FLOAT,

    SER_SP_ARRAY,
    SER_SP_PTR,
    _SER_SP_PARSABLE_COUNT,

    SER_SP_USER,
} ser_SpecPropKind;
// ^^^^VVVV order between these needs to stay consistant for parser to work properly
// user is intentionally left out because during parse it can't match with anything in this list
const char* ser_specPropKindParseNames[] = {
    "char",
    "int",
    "float",
    "arr",
    "ptr"
};

typedef struct  ser_SpecProp ser_SpecProp;
typedef struct ser_SpecUser ser_SpecUser;

struct ser_SpecProp {
    ser_SpecPropKind kind;
    ser_SpecProp* nextProp;

    ser_SpecProp* innerSpec; // valid for arr and ptr kinds, because they need more information

    // these two are filled in as soon as the spec is parsed from the user. The pointer needs to wait until
    // the whole set is validated
    const char* userSpecTag;
    int64_t userSpecTagLen;

    ser_SpecUser* userSpec; // filled in during specset validation

    // location of this member inside of the parent struct, from the start, in bytes, used for reading and
    // writing to structs in the program
    uint64_t parentStructOffset;
    uint64_t arrayLengthParentStructOffset;

    const char* tag;
    uint64_t tagLen;
};

struct ser_SpecUser {
    ser_SpecUserKind kind;
    ser_SpecUser* nextUserSpec;
    const char* tag;

    uint64_t id; // calcualted on lock, just the index in the specSet list

    union {
        struct {
            ser_SpecProp* structFirstChild;
            uint64_t structSize;
            uint64_t structPropCount; // calcualted on lock
        };
        struct {
            uint64_t enumValCount;
            const char** enumVals;
        };
    };
};

typedef union {
    ser_SpecProp prop;
    ser_SpecUser user;
} ser_SpecUnion;

#define SER_MAX_SPECS 10000
typedef struct {
    ser_SpecUnion specs[SER_MAX_SPECS];
    int allocatedSpecCount;

    ser_SpecUser* firstUserSpec;
    ser_SpecUser* lastUserSpec;
    uint64_t specCount;

    bool isValidAndLocked;
} ser_SpecSet;

ser_SpecSet _globalSpecSet;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SPEC CONSTRUCTION
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

// retreives space for a spec from the set // doesn't do anything else
ser_SpecUnion* _ser_specPush(ser_SpecSet* set) {
    assert(!set->isValidAndLocked);
    assert(set->allocatedSpecCount < SER_MAX_SPECS);
    return &set->specs[set->allocatedSpecCount++];
}

// pushes a new user spec, fills in tag and kind, and adds it to the list for the set
ser_SpecUser* _ser_specUserPush(ser_SpecSet* set, const char* tag, ser_SpecUserKind kind) {
    assert(!set->isValidAndLocked);
    ser_SpecUser* s = &_ser_specPush(set)->user;
    s->tag = tag;
    s->kind = kind;

    // push to the list of user specs
    if (!set->firstUserSpec) {
        set->firstUserSpec = s;
        set->lastUserSpec = s;
    } else {
        set->lastUserSpec->nextUserSpec = s;
        set->lastUserSpec = s;
    }
    return s;
}

// null on failure, reports the first seen spec in the set
ser_SpecUser* _ser_specUserGetByTag(ser_SpecSet* set, const char* tag, uint64_t tagLen) {
    for (ser_SpecUser* s = set->firstUserSpec; s; s = s->nextUserSpec) {
        if (tagLen == strlen(s->tag)) {
            if (strncmp(tag, s->tag, tagLen) == 0) { // TODO: fuck you stl
                return s;
            }
        }
    }
    return NULL;
}

ser_Error _ser_tryPatchPropUserRef(ser_SpecSet* set, ser_SpecProp* p) {
    if (p->kind != SER_SP_USER) {
        if (p->kind == SER_SP_ARRAY || p->kind == SER_SP_PTR) {
            return _ser_tryPatchPropUserRef(set, p->innerSpec); // drill into non-terminals to find end refs
        }
        return SERE_OK; // in the case where there is a terminal and it wasn't a SER_SP_USER node, all good
    }

    ser_SpecUser* userSpec = _ser_specUserGetByTag(set, p->userSpecTag, p->userSpecTagLen);
    if (userSpec == NULL) {
        return SERE_UNRESOLVED_USER_SPEC_TAG;
    }
    p->userSpec = userSpec;

    return SERE_OK;
}

// TODO: test if this works getting called on an already validated/locked specset
ser_Error _ser_validateAndLockSpecSet(ser_SpecSet* set) {
    // fill in IDs of each user spec, collect the count of userspecs in the list
    for (ser_SpecUser* userSpec = set->firstUserSpec; userSpec; userSpec = userSpec->nextUserSpec) {
        userSpec->id = set->specCount;
        set->specCount++;

        // make sure this name isn't in conflict with any others
        for (ser_SpecUser* other = userSpec->nextUserSpec; other; other = other->nextUserSpec) {
            if (strcmp(other->tag, userSpec->tag) == 0) {
                return SERE_DUPLICATE_USER_SPEC_NAMES;
            }
        }
    }
    _SER_EXPECT(set->specCount != 0, SERE_SPECSET_EMPTY);

    // TODO: fail when offsets haven't been set
    // TODO: fail on circular struct composition

    // once IDs are filled in, go back and patch any userSpec prop refs
    // TODO: pointers to every ref could be found at spec construction time and this search wouldn't have to happen
    // TODO: inner prop strs shouldn't be null
    for (ser_SpecUser* userSpec = set->firstUserSpec; userSpec; userSpec = userSpec->nextUserSpec) {
        if (userSpec->kind == SER_SU_STRUCT) {
            int64_t propCount = 0;
            for (ser_SpecProp* prop = userSpec->structFirstChild; prop; prop = prop->nextProp) {
                // link any refs with actual pointers
                // TODO: this is using tags to find and patch RN, might need to change this when specs from files come in
                ser_Error e = _ser_tryPatchPropUserRef(set, prop);
                _SER_VALID_OR_RETURN(e);
                propCount++;

                // check duplicates
                for (ser_SpecProp* other = prop->nextProp; other; other = other->nextProp) {
                    if (other->tagLen == prop->tagLen) {
                        if (strncmp(other->tag, prop->tag, prop->tagLen) == 0) {
                            return SERE_DUPLICATE_PROP_NAMES;
                        }
                    }
                }
            }
            userSpec->structPropCount = propCount;
        } else if (userSpec->kind == SER_SU_ENUM) {
            _SER_EXPECT(userSpec->enumVals != NULL, SERE_EMPTY_ENUM);
            _SER_EXPECT(userSpec->enumValCount != 0, SERE_EMPTY_ENUM);
        } else {
            assert(false);
        }
    }

    set->isValidAndLocked = true;
    return SERE_OK;
}

// public function used to indicate that every spec is done being constructed.
// name is wierd because the user will never have to deal with >1 specset.
// validates and locks the global set.
void ser_lockSpecs() {
    assert(_globalSpecSet.isValidAndLocked == false);
    assert(_ser_validateAndLockSpecSet(&_globalSpecSet) == SERE_OK);
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
ser_SpecProp* _ser_specParsePropInners(const char** str) {
    ser_SpecProp* firstInner = NULL;
    ser_SpecProp* lastInner = NULL;

    bool expectingAnotherToken = false;
    while (true) {
        const char* kindStr;
        uint64_t kindStrLen;
        bool newToken = _ser_parseToken(str, &kindStr, &kindStrLen);
        if (!newToken) {
            assert(!expectingAnotherToken); // non terminals require another token after them, if there are none left but we are expecting one, fail
            break;
        }

        ser_SpecPropKind k = SER_SP_USER; // default to user kind if no keywords match
        for (uint64_t i = 0; i < _SER_SP_PARSABLE_COUNT; i++) {
            const char* keywordCandidate = ser_specPropKindParseNames[i];
            if (strlen(keywordCandidate) == kindStrLen) {
                if (strncmp(ser_specPropKindParseNames[i], kindStr, kindStrLen) == 0) {
                    k = i;
                    break;
                }
            }
        } // end matching keywords for prop kinds

        ser_SpecProp* inner = &_ser_specPush(&_globalSpecSet)->prop;
        inner->kind = k;

        if (inner->kind == SER_SP_USER) {
            inner->userSpecTag = kindStr;
            inner->userSpecTagLen = kindStrLen;
        }

        if (!firstInner) {
            firstInner = inner;
            lastInner = inner;
        } else {
            // push to the back of the list
            lastInner->innerSpec = inner;
            lastInner = inner;
        }

        if (inner->kind != SER_SP_ARRAY && inner->kind != SER_SP_PTR) {
            break; // reached a terminal, stop looking for more inners
        } else {
            expectingAnotherToken = true;
        }
    }
    return firstInner;
}

void _ser_specStruct(const char* tag, const char* str) {
    assert(!_globalSpecSet.isValidAndLocked);
    ser_SpecUser* userSpec = _ser_specUserPush(&_globalSpecSet, tag, SER_SU_STRUCT);

    ser_SpecProp* firstProp = NULL;
    ser_SpecProp* lastProp = NULL;

    const char* c = str;
    while (true) {
        const char* propName = NULL;
        uint64_t propNameLen = 0;
        bool newToken = _ser_parseToken(&c, &propName, &propNameLen);
        if (!newToken) {
            break;
        }
        ser_SpecProp* firstKindSpec = _ser_specParsePropInners(&c);
        assert(firstKindSpec != NULL);
        firstKindSpec->tag = propName;
        firstKindSpec->tagLen = propNameLen;

        if (firstProp == NULL) {
            firstProp = firstKindSpec;
            lastProp = firstKindSpec;
        } else {
            lastProp->nextProp = firstKindSpec;
            lastProp = firstKindSpec;
        }
    }

    assert(firstProp);
    userSpec->structFirstChild = firstProp;
}

void _ser_specEnum(const char* tag, const char* strs[], int count) {
    assert(!_globalSpecSet.isValidAndLocked);
    ser_SpecUser* s = _ser_specUserPush(&_globalSpecSet, tag, SER_SU_ENUM);
    s->enumVals = strs;
    s->enumValCount = count;
}

// TODO: some type of gaurd for wrong number of passed or recieved args
void _ser_specStructOffsets(const char* tag, int structSize, ...) {
    assert(!_globalSpecSet.isValidAndLocked);
    va_list args;
    va_start(args, structSize);

    ser_SpecUser* structSpec = _ser_specUserGetByTag(&_globalSpecSet, tag, strlen(tag));
    assert(structSpec != NULL);
    assert(structSpec->kind == SER_SU_STRUCT);
    structSpec->structSize = structSize;

    for (ser_SpecProp* prop = structSpec->structFirstChild; prop; prop = prop->nextProp) {
        int64_t offset = va_arg(args, uint64_t);
        prop->parentStructOffset = offset;

        if (prop->kind == SER_SP_ARRAY) {
            prop->arrayLengthParentStructOffset = va_arg(args, uint64_t);
        }
    }
    va_end(args);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SERIALIZATION
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define _SER_LOOKUP_MEMBER(outT, obj, offset) ((outT*)(((char*)obj) + (offset)))

// VAR WILL BE DOUBLE EVALED // TODO: DON'T
#define _SER_WRITE_VAR_OR_FAIL(var, file) _SER_WRITE_OR_FAIL(&var, sizeof(var), file)

#define _SER_WRITE_OR_FAIL(ptr, size, file)    \
    do {                                       \
        if(fwrite(ptr, size, 1, file) != 1) { \
            return SERE_FWRITE_FAILED;         \
        }                                      \
    } while(0)

// TODO: be able to have more than one spec going instead of always using the global one
// TODO: rename prop spec to something not bad

/*
The File Format:
uint64_t endian indicator
uint64_t version no
uint64_t specCount
    uint8_t kind // direct from the enum // annoying that it's that big, but ok
    [tag string]
    [null terminator] // TODO: probably bad for a file format
    if enum:
        uint64_t valCount
            [value string]
            [null term]
    if struct:
        uint64_t propCount
            [tag string]
            [null term]
            prop
                    // prop
                        uint8_t kind
                        if user:
                            uint64_t id
                        if non-terminal:
                            // <- inner prop goes here

uint64_t object count
    uint64_t spec ID (index)
        parse by prop order dictated in the spec :)
        where arrays are a uint64_t for count and then repeated inner elements
*/

int64_t _ser_sizeOfPropStruct(ser_SpecProp* p) {
    if (p->kind == SER_SP_CHAR) {
        return sizeof(char);
    } else if (p->kind == SER_SP_INT) {
        return sizeof(int);
    } else if (p->kind == SER_SP_FLOAT) {
        return sizeof(float);
    } else if (p->kind == SER_SP_ARRAY) {
        assert(false);
    } else if (p->kind == SER_SP_PTR) {
        assert(false); // TODO: the pointer thing
    } else if (p->kind == SER_SP_USER) {
        assert(p->userSpec->kind == SER_SU_STRUCT); // TODO: this is covered elsewhere, right?
        return p->userSpec->structSize;
    } else {
        assert(false);
    }
}

ser_Error _ser_serializeStructByPropSpec(FILE* file, void* obj, ser_SpecProp* spec);
ser_Error _ser_serializeStructByUserSpec(FILE* file, void* obj, ser_SpecUser* spec);

ser_Error _ser_serializeStructByPropSpec(FILE* file, void* obj, ser_SpecProp* spec) {
    if (spec->kind == SER_SP_FLOAT) {
        _SER_WRITE_OR_FAIL(obj, sizeof(float), file);
    } else if (spec->kind == SER_SP_INT) {
        _SER_WRITE_OR_FAIL(obj, sizeof(int), file);
    } else if (spec->kind == SER_SP_CHAR) {
        _SER_WRITE_OR_FAIL(obj, sizeof(char), file);
    } else if (spec->kind == SER_SP_USER) {
        return _ser_serializeStructByUserSpec(file, obj, spec->userSpec);
    } else if (spec->kind == SER_SP_ARRAY) {
        // TODO: assuming type, will fuck w you later
        int64_t arrCount = *_SER_LOOKUP_MEMBER(int64_t, obj, spec->arrayLengthParentStructOffset);
        _SER_WRITE_VAR_OR_FAIL(arrCount, file);

        int64_t innerSize = _ser_sizeOfPropStruct(spec->innerSpec); // TODO: array of arrays now cannot happen. document or fix
        for (int64_t i = 0; i < arrCount; i++) {
            int64_t offset = spec->parentStructOffset + (i * innerSize);
            void* ptr = _SER_LOOKUP_MEMBER(void, obj, offset);
            ser_Error e = _ser_serializeStructByPropSpec(file, ptr, spec->innerSpec);
            _SER_VALID_OR_RETURN(e);
        }
    } else if (spec->kind == SER_SP_PTR) {
        assert(false);
    } else {
        assert(false);
    }
    return SERE_OK;
}

ser_Error _ser_serializeStructByUserSpec(FILE* file, void* obj, ser_SpecUser* spec) {
    for (ser_SpecProp* prop = spec->structFirstChild; prop; prop = prop->nextProp) {
        void* propLoc = _SER_LOOKUP_MEMBER(void, obj, prop->parentStructOffset);
        _ser_serializeStructByPropSpec(file, propLoc, prop);
    }
    return SERE_OK;
}

ser_Error _ser_serializeProp(FILE* file, ser_SpecProp* prop) {
    uint8_t kind = prop->kind;
    assert(prop->kind < 255 && prop->kind >= 0);
    _SER_WRITE_VAR_OR_FAIL(kind, file);

    if (prop->kind == SER_SP_USER) {
        uint64_t id = prop->userSpec->id;
        _SER_WRITE_VAR_OR_FAIL(id, file);
    }

    if (prop->kind == SER_SP_ARRAY || prop->kind == SER_SP_PTR) {
        _ser_serializeProp(file, prop->innerSpec);
    }
    return SERE_OK;
}

// TODO: remove as many asserts as possible

ser_Error _ser_writeObjectToFileInner(const char* type, void* obj, uint64_t userSpecCount, FILE* file) {
    uint64_t endianIndicator = 1;
    _SER_WRITE_VAR_OR_FAIL(endianIndicator, file);
    uint64_t versionNo = 0;
    _SER_WRITE_VAR_OR_FAIL(versionNo, file);
    _SER_WRITE_VAR_OR_FAIL(userSpecCount, file);

    for (ser_SpecUser* userSpec = _globalSpecSet.firstUserSpec; userSpec; userSpec = userSpec->nextUserSpec) {
        assert(userSpec->kind < 255 && userSpec->kind >= 0);
        uint8_t specKind = (uint8_t)(userSpec->kind);
        _SER_WRITE_VAR_OR_FAIL(specKind, file);

        _SER_WRITE_OR_FAIL(userSpec->tag, strlen(userSpec->tag) + 1, file); // will include the null term, always writes something fingers crossed

        if (userSpec->kind == SER_SU_ENUM) {
            uint64_t valCount = userSpec->enumValCount;
            _SER_WRITE_VAR_OR_FAIL(valCount, file);
            for (uint64_t enumValIdx = 0; enumValIdx < valCount; enumValIdx++) {
                const char* val = userSpec->enumVals[enumValIdx];
                _SER_WRITE_OR_FAIL(val, strlen(val) + 1, file); // will include the null term, always writes something fingers crossed
            }
        } else if (userSpec->kind == SER_SU_STRUCT) {
            uint64_t propCount = userSpec->structPropCount;
            _SER_WRITE_VAR_OR_FAIL(propCount, file);
            for (ser_SpecProp* prop = userSpec->structFirstChild; prop; prop = prop->nextProp) {
                assert(prop->tagLen != 0);
                _SER_WRITE_OR_FAIL(prop->tag, prop->tagLen, file);
                uint8_t nullTerm = '\0';
                _SER_WRITE_VAR_OR_FAIL(nullTerm, file);
                _ser_serializeProp(file, prop);
            }
        } else {
            assert(false);
        }
    }

    // OBJECTS =============================================================
    ser_SpecUser* spec = _ser_specUserGetByTag(&_globalSpecSet, type, strlen(type));
    assert(spec->kind == SER_SU_STRUCT);
    uint64_t id = spec->id;
    _SER_WRITE_VAR_OR_FAIL(id, file);
    _ser_serializeStructByUserSpec(file, obj, spec);
    return SERE_OK;
}

// this function got split so that early error returns from the inner still close the file properly
// wish I could have inlined the function but c sucks
ser_Error ser_writeObjectToFile(const char* path, const char* type, void* obj) {
    _SER_EXPECT(_globalSpecSet.isValidAndLocked == true, SERE_SPECSET_UNLOCKED);

    FILE* file = fopen(path, "wb");
    _SER_EXPECT(file != NULL, SERE_FOPEN_FAILED);

    ser_Error e = _ser_writeObjectToFileInner(type, obj, _globalSpecSet.specCount, file);
    assert(fclose(file) == 0);
    return e;
}

#define _SER_STRINGIZE(x) #x
#define ser_specStruct(T, str) _ser_specStruct(_SER_STRINGIZE(T), _SER_STRINGIZE(str))
#define ser_specEnum(T, strs, count) _ser_specEnum(_SER_STRINGIZE(T), strs, count)

#define _SER_OFFSET2(T, a) offsetof(T, a)
#define _SER_OFFSET3(T, a, b) offsetof(T, a), offsetof(T, b)
#define _SER_OFFSET4(T, a, b, c) offsetof(T, a), offsetof(T, b), offsetof(T, c)
#define _SER_OFFSET5(T, a, b, c, d) offsetof(T, a), offsetof(T, b), offsetof(T, c), offsetof(T, d)
#define _SER_OFFSET6(T, a, b, c, d, e) offsetof(T, a), offsetof(T, b), offsetof(T, c), offsetof(T, d), offsetof(T, e)
#define _SER_OFFSET7(T, a, b, c, d, e, f) offsetof(T, a), offsetof(T, b), offsetof(T, c), offsetof(T, d), offsetof(T, e), offsetof(T, f)
#define _SER_OFFSET8(T, a, b, c, d, e, f, g) offsetof(T, a), offsetof(T, b), offsetof(T, c), offsetof(T, d), offsetof(T, e), offsetof(T, f), offsetof(T, g)

#define _SER_SELECT_BY_PARAM_COUNT(_1,_2,_3,_4,_5,_6,_7,_8,NAME,...) NAME
#define _SER_OFFSET_SWITCH(...) _SER_SELECT_BY_PARAM_COUNT(__VA_ARGS__, _SER_OFFSET8, _SER_OFFSET7, _SER_OFFSET6, _SER_OFFSET5, _SER_OFFSET4, _SER_OFFSET3, _SER_OFFSET2)(__VA_ARGS__)
#define ser_specStructOffsets(T, ...) _ser_specStructOffsets(_SER_STRINGIZE(T), sizeof(T), _SER_OFFSET_SWITCH(T, __VA_ARGS__))

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

// doesn't check for a null terminator in the file, though expected should be null terminated
ser_Error _ser_test_expectString(const char* expected, FILE* file) {
    uint64_t len = strlen(expected);
    assert(len != 0);
    char* got = malloc(len);
    assert(got);
    _SER_EXPECT(fread(got, len, 1, file) == 1, SERE_FREAD_FAILED);
    _SER_EXPECT(memcmp(got, expected, len) == 0, SERE_TEST_EXPECT_FAILED);
    free(got);
    return SERE_OK;
}

void _ser_test_clearGlobalSpecSet() {
    memset(&_globalSpecSet, 0, sizeof(ser_SpecSet));
}

ser_Error _ser_test_serializeVec2() {
    _ser_test_clearGlobalSpecSet();

    ser_specStruct(HMM_Vec2,
                   X float
                   Y float);
    ser_specStructOffsets(HMM_Vec2, X, Y);

    _SER_VALID_OR_RETURN(_ser_validateAndLockSpecSet(&_globalSpecSet));

    HMM_Vec2 v = HMM_V2(69, 420);
    _SER_VALID_OR_RETURN(ser_writeObjectToFile("./testing/file1", "HMM_Vec2", &v));

    FILE* f = fopen("./testing/file1", "rb");
    if (!f) {
        return SERE_FOPEN_FAILED;
    }

    _SER_TEST_READ(uint64_t, 1, f); // endian
    _SER_TEST_READ(uint64_t, 0, f); // version
    _SER_TEST_READ(uint64_t, 1, f); // spec count

    _SER_TEST_READ(uint8_t, SER_SU_STRUCT, f); // first spec is a struct
    _SER_VALID_OR_RETURN(_ser_test_expectString("HMM_Vec2", f)); // name
    _SER_TEST_READ(uint8_t, 0, f); // null terminator
    _SER_TEST_READ(uint64_t, 2, f); // prop count

    _SER_VALID_OR_RETURN(_ser_test_expectString("X", f));
    _SER_TEST_READ(uint8_t, 0, f); // null term
    _SER_TEST_READ(uint8_t, SER_SP_FLOAT, f); // kind should be a float

    _SER_VALID_OR_RETURN(_ser_test_expectString("Y", f));
    _SER_TEST_READ(uint8_t, 0, f); // null term
    _SER_TEST_READ(uint8_t, SER_SP_FLOAT, f); // kind should be a float

    _SER_TEST_READ(uint64_t, 0, f); // origin spec should be index 0, the vector
    _SER_TEST_READ(float, 69, f);
    _SER_TEST_READ(float, 420, f);

    uint8_t eofProbe = 0;
    _SER_EXPECT(fread(&eofProbe, sizeof(uint8_t), 1, f) == 0, SERE_TEST_EXPECT_FAILED);

    fclose(f);
    return SERE_OK;
}

void ser_tests() {
    test_printSectionHeader("Ser");

    test_printResult(_ser_test_serializeVec2() == SERE_OK, "serialize vector");

    {
        _ser_test_clearGlobalSpecSet();
        ser_specStruct(myStruct,
                       x int
                       y float
                       x char);
        ser_Error e = _ser_validateAndLockSpecSet(&_globalSpecSet);
        test_printResult(e == SERE_DUPLICATE_PROP_NAMES, "duplicate props");
    }

    {
        _ser_test_clearGlobalSpecSet();
        ser_specStruct(myStruct, x int);
        const char* vals[] = { "hello" };
        ser_specEnum(myStruct, vals, 1);
        ser_Error e = _ser_validateAndLockSpecSet(&_globalSpecSet);
        test_printResult(e == SERE_DUPLICATE_USER_SPEC_NAMES, "duplicate user specs");
    }

    {
        _ser_test_clearGlobalSpecSet();
        const char* vals[] = { "hello" };
        ser_specEnum(enum, vals, 0);
        ser_Error e = _ser_validateAndLockSpecSet(&_globalSpecSet);
        test_printResult(e == SERE_EMPTY_ENUM, "empty enum");
    }

    {
        _ser_test_clearGlobalSpecSet();
        ser_specEnum(enum, NULL, 10);
        ser_Error e = _ser_validateAndLockSpecSet(&_globalSpecSet);
        test_printResult(e == SERE_EMPTY_ENUM, "empty enum 2");
    }

    {
        _ser_test_clearGlobalSpecSet();
        ser_specStruct(struct1,
                       next struct2
                       prev struct2);

        ser_Error e = _ser_validateAndLockSpecSet(&_globalSpecSet);
        test_printResult(e == SERE_UNRESOLVED_USER_SPEC_TAG, "unresolved user spec ref");
    }

    // TODO: solution for testing failure cases where asserts are

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