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
    // every spec is done and ser_specValidate() is called
    const char* userSpecTag;
    int64_t userSpecTagLen;
    ser_SpecUser* userSpec; // filled in during ser_specValidate() // TODO: this function does not exist anymore

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

    uint64_t id; // filled in by ser_specValidate()

    union {
        struct {
            ser_SpecProp* structFirstChild;
            uint64_t structSize;
            uint64_t structPropCount; // filled in by ser_specValidate()
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
} ser_Globs;
ser_Globs globs;

ser_SpecUnion* _ser_specPush() {
    assert(globs.allocatedSpecCount < SER_MAX_SPECS);
    return &globs.specs[globs.allocatedSpecCount++];
}

ser_SpecUser* _ser_specUserPush(const char* tag, ser_SpecUserKind kind) {
    ser_SpecUser* s = &_ser_specPush()->user;
    s->tag = tag;
    s->kind = kind;

    // push to the list of user specs
    if (!globs.firstUserSpec) {
        globs.firstUserSpec = s;
        globs.lastUserSpec = s;
    } else {
        globs.lastUserSpec->nextUserSpec = s;
        globs.lastUserSpec = s;
    }
    return s;
}

// TODO: flag for when a set of specs has been validated
// TODO: new spec validation
// TODO: name duplicate check

// null on failure, reports the first seen spec in the global list
ser_SpecUser* _ser_specUserGet(const char* tag, uint64_t tagLen) {
    for (ser_SpecUser* s = globs.firstUserSpec; s; s = s->nextUserSpec) {
        if (tagLen == strlen(s->tag)) {
            if (strncmp(tag, s->tag, tagLen) == 0) { // TODO: fuck you stl
                return s;
            }
        }
    }
    return NULL;
}

// ptr is read and written to // should be inside of a null terminated string
// return is success or failure
// out params unwritten on a failure return
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
ser_SpecProp* _ser_specParsePropInners(const char** str) {
    ser_SpecProp* firstInner = NULL;
    ser_SpecProp* lastInner = NULL;

    while (true) {
        const char* kindStr;
        uint64_t kindStrLen;
        bool newToken = _ser_parseToken(str, &kindStr, &kindStrLen);
        if (!newToken) {
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


        ser_SpecProp* inner = &_ser_specPush()->prop;
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
        }
    }
    return firstInner;
}

void _ser_specStruct(const char* tag, const char* str) {
    ser_SpecUser* userSpec = _ser_specUserPush(tag, SER_SU_STRUCT);

    ser_SpecProp* firstProp = NULL;

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

        firstKindSpec->nextProp = firstProp;
        firstProp = firstKindSpec;
    }

    assert(firstProp);
    userSpec->structFirstChild = firstProp;
}

void _ser_specEnum(const char* tag, const char* strs[], int count) {
    ser_SpecUser* s = _ser_specUserPush(tag, SER_SU_ENUM);
    s->enumVals = strs;
    s->enumValCount = count;
}

void _ser_specStructOffsets(const char* tag, int structSize, ...) {
    va_list args;
    va_start(args, structSize);

    ser_SpecUser* structSpec = _ser_specUserGet(tag, strlen(tag));
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

#define _SER_LOOKUP_MEMBER(outT, obj, offset) ((outT*)(((char*)obj) + (offset)))

void _ser_serializeStructByPropSpec(FILE* file, void* obj, ser_SpecProp* spec);
void _ser_serializeStructByUserSpec(FILE* file, void* obj, ser_SpecUser* spec);

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

void _ser_serializeStructByPropSpec(FILE* file, void* obj, ser_SpecProp* spec) {
    if (spec->kind == SER_SP_FLOAT) {
        fwrite(obj, 1, 4, file);
    } else if (spec->kind == SER_SP_INT) {
        fwrite(obj, 1, 4, file);
    } else if (spec->kind == SER_SP_CHAR) {
        fwrite(obj, 1, 1, file);
    } else if (spec->kind == SER_SP_USER) {
        _ser_serializeStructByUserSpec(file, obj, spec->userSpec);
    } else if (spec->kind == SER_SP_ARRAY) {
        // TODO: assuming type, will fuck w you later
        int64_t arrCount = *_SER_LOOKUP_MEMBER(int64_t, obj, spec->arrayLengthParentStructOffset);
        fwrite(&arrCount, 8, 1, file);

        int64_t innerSize = _ser_sizeOfPropStruct(spec->innerSpec); // TODO: array of arrays now cannot happen. document or fix
        for (int64_t i = 0; i < arrCount; i++) {
            int64_t offset = spec->parentStructOffset + (i * innerSize);
            void* ptr = _SER_LOOKUP_MEMBER(void, obj, offset);
            _ser_serializeStructByPropSpec(file, ptr, spec->innerSpec);
        }
    } else if (spec->kind == SER_SP_PTR) {
        assert(false);
    } else {
        assert(false);
    }
}

void _ser_serializeStructByUserSpec(FILE* file, void* obj, ser_SpecUser* spec) {
    for (ser_SpecProp* prop = spec->structFirstChild; prop; prop = prop->nextProp) {
        void* propLoc = _SER_LOOKUP_MEMBER(void, obj, prop->parentStructOffset);
        _ser_serializeStructByPropSpec(file, propLoc, prop);
    }
}

typedef enum {
    SERE_OK,
    SERE_FOPEN_FAILED,
    SERE_BAD_POINTER,
    SERE_UNRESOLVED_USER_SPEC_TAG,
} ser_Error;

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

uint64_t origin spec ID (index)
    parse by prop order dictated in the spec :)
    where arrays are a uint64_t for count and then repeated inner elements

*/

ser_Error _ser_tryPatchPropUserRef(ser_SpecProp* p) {
    if (p->kind != SER_SP_USER) {
        if (p->kind == SER_SP_ARRAY || p->kind == SER_SP_PTR) {
            return _ser_tryPatchPropUserRef(p->innerSpec); // drill into non-terminals to find end refs
        }
        return SERE_OK; // in the case where there is a terminal and it wasn't a SER_SP_USER node, all good
    }

    ser_SpecUser* userSpec = _ser_specUserGet(p->userSpecTag, p->userSpecTagLen);
    if (userSpec == NULL) {
        return SERE_UNRESOLVED_USER_SPEC_TAG;
    }
    p->userSpec = userSpec;

    return SERE_OK;
}

void _ser_serializeProp(FILE* file, ser_SpecProp* prop) {
    uint8_t kind = prop->kind;
    assert(prop->kind < 255 && prop->kind >= 0);
    fwrite(&kind, sizeof(kind), 1, file);

    if (prop->kind == SER_SP_USER) {
        uint64_t id = prop->userSpec->id;
        fwrite(&id, sizeof(id), 1, file);
    }

    if (prop->kind == SER_SP_ARRAY || prop->kind == SER_SP_PTR) {
        _ser_serializeProp(file, prop->innerSpec);
    }
}

ser_Error ser_writeObjectToFile(void* obj, const char* type) {
    const char* path = "./testing/file1";
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        printf("file open failed\n");
        return SERE_FOPEN_FAILED;
    }

    // HEADER ===================================
    uint64_t endianIndicator = 1;
    fwrite(&endianIndicator, sizeof(endianIndicator), 1, file);
    uint64_t versionNo = 0;
    fwrite(&versionNo, sizeof(versionNo), 1, file);

    // TREE PREPROCESSING ==============================================
    uint64_t userSpecCount = 0;
    {
        // fill in IDs of each user spec, collect the count of userspecs in the list
        for (ser_SpecUser* userSpec = globs.firstUserSpec; userSpec; userSpec = userSpec->nextUserSpec) {
            userSpec->id = userSpecCount; // ends up being index into the list
            userSpecCount++;
        }

        // TODO: duplicate checking here :) ^^^^^^^^^^^^^^^^^^^^^^^^^

        // once IDs are filled in, go back and patch any userSpec prop refs
        // TODO: pointers to every ref could be found at spec construction time and this search wouldn't have to happen
        // TODO: inner prop strs shouldn't be null
        for (ser_SpecUser* userSpec = globs.firstUserSpec; userSpec; userSpec = userSpec->nextUserSpec) {
            if (userSpec->kind == SER_SU_STRUCT) {
                int64_t propCount = 0;
                for (ser_SpecProp* prop = userSpec->structFirstChild; prop; prop = prop->nextProp) {
                    ser_Error e = _ser_tryPatchPropUserRef(prop);
                    if (e != SERE_OK) {
                        return e;
                    }
                    propCount++;
                }
                userSpec->structPropCount = propCount;
            }
        }
        assert(userSpecCount != 0); // can't hurt
    }

    // SPEC + NODES ====================================================
    fwrite(&userSpecCount, sizeof(userSpecCount), 1, file);

    for (ser_SpecUser* userSpec = globs.firstUserSpec; userSpec; userSpec = userSpec->nextUserSpec) {
        uint8_t specKind = (uint8_t)(userSpec->kind);
        assert(userSpec->kind < 255 && userSpec->kind >= 0);
        fwrite(&specKind, sizeof(specKind), 1, file);
        fwrite(userSpec->tag, strlen(userSpec->tag), 1, file); // nothing written when length is 0
        fwrite("\0", 1, 1, file);

        if (userSpec->kind == SER_SU_ENUM) {
            uint64_t valCount = userSpec->enumValCount;
            fwrite(&valCount, sizeof(valCount), 1, file);
            for (uint64_t enumValIdx = 0; enumValIdx < valCount; enumValIdx++) {
                const char* val = userSpec->enumVals[enumValIdx];
                fwrite(val, strlen(val), 1, file);
                fwrite("\0", 1, 1, file);
            }
        } else if (userSpec->kind == SER_SU_STRUCT) {
            uint64_t propCount = userSpec->structPropCount;
            fwrite(&propCount, sizeof(propCount), 1, file);
            for (ser_SpecProp* prop = userSpec->structFirstChild; prop; prop = prop->nextProp) {
                fwrite(prop->tag, prop->tagLen, 1, file);
                fwrite("\0", 1, 1, file);
                _ser_serializeProp(file, prop);
            }
        } else {
            assert(false);
        }
    }

    // OBJECTS =============================================================
    ser_SpecUser* spec = _ser_specUserGet(type, strlen(type));
    assert(spec->kind == SER_SU_STRUCT);
    fwrite(&spec->id, sizeof(spec->id), 1, file);
    _ser_serializeStructByUserSpec(file, obj, spec);
    return SERE_OK;
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

#include "HMM/HandmadeMath.h"
#include "sketches.h"

void ser_tests() {
    ser_specStruct(HMM_Vec2,
                   X float
                   Y float);
    ser_specStructOffsets(HMM_Vec2, X, Y);

    const char* lknames[] = { "straight", "arc" };
    ser_specEnum(sk_LineKind, lknames, sizeof(lknames) / sizeof(const char*));
    ser_specStruct(sk_Line,
                   kind   sk_LineKind
                   p1     ptr HMM_Vec2
                   p2     ptr HMM_Vec2
                   center ptr HMM_Vec2);
    ser_specStructOffsets(sk_Line, kind, p1, p2, center);

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

    // _ser_printState();

    HMM_Vec2 myVector = HMM_V2(69, 420);
    ser_writeObjectToFile(&myVector, "HMM_Vec2");
}