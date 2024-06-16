
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
} _ser_NodeKind;

const char* ser_nodeKindParsableNames[] = {
    "",
    "arr",
    "float",
    "int",
    "char",
};

typedef struct _ser_Node _ser_Node;
struct _ser_Node {
    bool isTopLevel;
    const char* tag;
    int tagLen;
    _ser_NodeKind kind;

    _ser_Node* otherUserNode;
    const char** enumValues;
    int enumCount;

    _ser_Node* nextSibling;
};

#define SER_MAX_NODES 10000
typedef struct {
    _ser_Node nodes[SER_MAX_NODES];
    int nodeCount;
} ser_Globs;
ser_Globs globs;

// asserts on failure
_ser_Node* _ser_nodePush(const char* tag, int tagLen, _ser_NodeKind kind, bool asSibling) {
    assert(globs.nodeCount < SER_MAX_NODES);
    _ser_Node* s = &globs.nodes[globs.nodeCount++];
    s->tag = tag;
    s->tagLen = tagLen;
    s->kind = kind;
    s->isTopLevel = true;

    if (asSibling) {
        assert(globs.nodeCount > 1);
        _ser_Node* prev = &globs.nodes[globs.nodeCount - 2];
        prev->nextSibling = s;
        s->isTopLevel = false;
    }
    return s;
}

// null on failure
_ser_Node* _ser_nodeGetByTag(const char* tag, int tagLen) {
    for (int i = 0; i < globs.nodeCount; i++) {
        _ser_Node* s = &globs.nodes[i];
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

void _ser_specStruct(const char* tag, const char* str) {
    _ser_nodePush(tag, strlen(tag), SER_SK_STRUCT, false);

    const char* c = str;
    while (true) {
        const char* name = NULL;
        int nameLen = 0;
        if (_ser_parseToken(str, &c, &name, &nameLen)) {
            while (true) {
                const char* kind = NULL;
                int kindLen = 0;
                assert(_ser_parseToken(str, &c, &kind, &kindLen));

                _ser_NodeKind k = SER_SK_OTHER_USER;
                // start at one so that index values line up with enum values, and so that none is skipped
                for (int i = 1; i < sizeof(ser_nodeKindParsableNames) / sizeof(const char*); i++) {
                    if (strncmp(ser_nodeKindParsableNames[i], kind, kindLen) == 0) {
                        k = i;
                        break;
                    }
                }

                // NOTE: every spec node inside of one prop (the inner inside an array/ref) will also have a name
                _ser_Node* spec = _ser_nodePush(name, nameLen, k, true);
                if (spec->kind == SER_SK_OTHER_USER) {
                    _ser_Node* other = _ser_nodeGetByTag(kind, kindLen);
                    assert(other != NULL);
                    spec->otherUserNode = other;
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

void _ser_specEnum(const char* tag, const char* strs[], int count) {
    _ser_Node* s = _ser_nodePush(tag, strlen(tag), SER_SK_ENUM, false);
    s->enumValues = strs;
    s->enumCount = count;
}

#define _SER_STRINGIZE(x) #x
#define ser_specStruct(T, str) _ser_specStruct(#T, _SER_STRINGIZE(str))
#define ser_specEnum(T, strs, count) _ser_specEnum(#T, strs, count)

void ser_tests() {
    ser_specStruct(HMM_Vec2,
                   X float
                   Y float);

    ser_specStruct(vectorArray,
                   vecs arr HMM_Vec2);

    // const char* lknames[] = { "straight", "arc" };
    // ser_specEnum(sk_LineKind, lknames, sizeof(lknames) / sizeof(const char*));
    // ser_specStruct(sk_Line,
    //                kind   sk_LineKind
    //                p1     ref HMM_Vec2
    //                p2     ref HMM_Vec2
    //                center ref HMM_Vec2);

    // const char* cknames[] = { "distance", "angleLines", "angleArc", "arcUniform", "axisAligned" };
    // ser_specEnum(sk_ConstraintKind, cknames, sizeof(cknames) / sizeof(const char*));
    // ser_specStruct(sk_Constraint,
    //                kind  sk_ConstraintKind
    //                line1 ref sk_Line
    //                line2 ref sk_Line
    //                value float);

    // ser_specStruct(sk_Sketch,
    //                points      arr HMM_Vec2
    //                lines       arr sk_Line
    //                constraints arr sk_Constraint);

    // print out created nodes for debugging / testing
    printf("\nNODES!\n");
    for (int i = 0; i < globs.nodeCount; i++) {
        _ser_Node* s = &globs.nodes[i];

        int link = (int)(s->otherUserNode - globs.nodes);
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
}
