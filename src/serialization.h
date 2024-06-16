
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    SER_SK_NONE,

    SER_SK_ARRAY,
    SER_SK_FLOAT,
    SER_SK_INT,
    SER_SK_CHAR,

    SER_SK_OTHER_USER,

    SER_SK_STRUCT,
    SER_SK_ENUM,
} ser_SpecKind;

const char* ser_specKindParsableNames[] = {
    "",
    "arr",
    "float",
    "int",
    "char",
};

typedef struct ser_Spec ser_Spec;
struct ser_Spec {
    bool isTopLevel;
    const char* tag;
    int tagLen;
    ser_SpecKind kind;

    ser_Spec* otherUserSpec;
    const char** enumValues;
    int enumCount;

    ser_Spec* nextSibling;
};

#define SER_MAX_SPECS 10000
typedef struct {
    ser_Spec specs[SER_MAX_SPECS];
    int specCount;
} ser_Globs;
ser_Globs globs;

// asserts on failure
ser_Spec* _ser_specPush(const char* tag, int tagLen, ser_SpecKind kind, bool asSibling) {
    assert(globs.specCount < SER_MAX_SPECS);
    ser_Spec* s = &globs.specs[globs.specCount++];
    s->tag = tag;
    s->tagLen = tagLen;
    s->kind = kind;
    s->isTopLevel = true;

    if (asSibling) {
        assert(globs.specCount > 1);
        ser_Spec* prev = &globs.specs[globs.specCount - 2];
        prev->nextSibling = s;
        s->isTopLevel = false;
    }
    return s;
}

// null on failure
ser_Spec* _ser_specGetByTag(const char* tag, int tagLen) {
    for (int i = 0; i < globs.specCount; i++) {
        ser_Spec* s = &globs.specs[i];
        if (!s->isTopLevel) {
            continue;
        } else if (strncmp(tag, s->tag, tagLen) == 0) {
            return s;
        }
    }
    return NULL;
}

// ptr is read and written to
// return is success or failure
// str expected to be null-terminated
// out params unwritten on a failure return
bool _ser_parseToken(const char* str, const char** ptr, const char** outTokStart, int* outTokLen) {
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
    *outTokLen = (int)(*ptr - tokStart);
    return tokStart != NULL;
}

void _ser_newSpecStruct(const char* tag, const char* str) {
    _ser_specPush(tag, strlen(tag), SER_SK_STRUCT, false);

    const char* c = str;
    while (true) {
        const char* name = NULL;
        int nameLen = 0;
        if (_ser_parseToken(str, &c, &name, &nameLen)) {
            while (true) {
                const char* kind = NULL;
                int kindLen = 0;
                assert(_ser_parseToken(str, &c, &kind, &kindLen));

                ser_SpecKind k = SER_SK_OTHER_USER;
                // start at one so that index values line up with enum values, and so that none is skipped
                for (int i = 1; i < sizeof(ser_specKindParsableNames) / sizeof(const char*); i++) {
                    if (strncmp(ser_specKindParsableNames[i], kind, kindLen) == 0) {
                        k = i;
                        break;
                    }
                }

                // NOTE: every spec node inside of one prop (the inner inside an array/ref) will also have a name
                ser_Spec* spec = _ser_specPush(name, nameLen, k, true);
                if (spec->kind == SER_SK_OTHER_USER) {
                    ser_Spec* other = _ser_specGetByTag(kind, kindLen);
                    assert(other != NULL);
                    spec->otherUserSpec = other;
                }

                if (spec->kind != SER_SK_ARRAY) {
                    break;  // end consuming kinds for this prop when reaching a terminal
                }
            }  // loop to consume a number of kinds in case of compound things like arr and ref
        }  // end check for name
        else {
            break;
        }
    }  // end prop while
}

void _ser_newSpecEnum(const char* tag, const char* strs[], int count) {
    ser_Spec* s = _ser_specPush(tag, strlen(tag), SER_SK_ENUM, false);
    s->enumValues = strs;
    s->enumCount = count;
}

#define _SER_STRINGIZE(x) #x
#define ser_newSpecStruct(T, str) _ser_newSpecStruct(#T, _SER_STRINGIZE(str))
#define ser_newSpecEnum(T, strs, count) _ser_newSpecEnum(#T, strs, count)

void ser_tests() {
    ser_newSpecStruct(HMM_Vec2,
                      X float
                      Y float);

    ser_newSpecEnum(myEnum, ser_specKindParsableNames, sizeof(ser_specKindParsableNames) / sizeof(const char*));

    ser_newSpecStruct(structThing,
                      V1 HMM_Vec2
                      V2 HMM_Vec2
                      wowEnum myEnum);

    ser_newSpecStruct(parent,
                      "structs arr structThing");

    // print out created specs for debugging / testing
    printf("\nSPECS!\n");
    for (int i = 0; i < globs.specCount; i++) {
        ser_Spec* s = &globs.specs[i];

        int link = (int)(s->otherUserSpec - globs.specs);
        if (s->kind != SER_SK_OTHER_USER) {
            link = -1;
        }
        const char* indent = "";
        if (!s->isTopLevel) {
            indent = "  ";
        }

        printf("%s%d: %-15.*s\tkind: ", indent, i, s->tagLen, s->tag);  // indent, index, tag
        if (s->kind == SER_SK_ENUM) {
            printf("enum\tvals: ");
            for (int valIndex = 0; valIndex < s->enumCount; valIndex++) {
                printf("\'%s\' ", s->enumValues[valIndex]);
            }
            printf("\n");
        } else if (s->kind == SER_SK_STRUCT) {
            printf("struct\n");
        } else {
            printf("%d\t\tother: %d\n", s->kind, link);
        }
    }

    // const char* lknames[] = {"straight", "arc"};
    // ser_newSpecEnum("sk_ser_lineKind", lknames);
    // ser_newSpecStruct(sk_Line,
    //                   "kind   sk_ser_LineKind "
    //                   "p1     ref HMM_Vec2 "
    //                   "p2     ref HMM_Vec2 "
    //                   "center ref HMM_Vec2 ");

    // const char* cknames[] = {"distance", "angleLines", "angleArc", "arcUniform", "axisAligned"};
    // ser_newSpecEnum(sk_ConstraintKind, cknames);
    // ser_newSpecStruct(sk_Constraint,
    //                   "kind  sk_ser_ConstraintKind "
    //                   "line1 ref sk_ser_Line "
    //                   "line2 ref sk_ser_Line "
    //                   "value float ");

    // ser_newSpecStruct(sk_Sketch,
    //                   "points      arr HMM_Vec2 "
    //                   "lines       arr sk_ser_Line "
    //                   "constraints arr sk_ser_Constraint ");
}
