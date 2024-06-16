
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    SER_NK_NONE,

    SER_NK_ARRAY,
    SER_NK_FLOAT,
    SER_NK_INT,
    SER_NK_CHAR,

    SER_NK_OTHER_USER,

    SER_NK_STRUCT,
    SER_NK_ENUM,
} _ser_NodeKind;

const char* ser_nodeKindParsableNames[] = {
    "",
    "arr",
    "float",
    "int",
    "char",
};

typedef enum {
    SER_ND_TOP, // top level, accessable by search, names can't conflict
    SER_ND_PROP, // beginning of a struct prop
    SER_ND_INNER, // node inside of a prop - where the first one is an arr or ref
} _ser_NodeDepth;

typedef struct _ser_Node _ser_Node;
struct _ser_Node {
    _ser_NodeDepth depth;
    const char* tag;
    int tagLen;
    _ser_NodeKind kind;

    _ser_Node* otherUserNode;
    const char** enumValues;
    int enumCount;
};

#define SER_MAX_NODES 10000
typedef struct {
    _ser_Node nodes[SER_MAX_NODES];
    int nodeCount;
} ser_Globs;
ser_Globs globs;

// asserts on failure
_ser_Node* _ser_nodePush(const char* tag, int tagLen, _ser_NodeKind kind, _ser_NodeDepth depth) {
    assert(globs.nodeCount < SER_MAX_NODES);
    _ser_Node* s = &globs.nodes[globs.nodeCount++];
    s->tag = tag;
    s->tagLen = tagLen;
    s->kind = kind;
    s->depth = depth;
    return s;
}

// null on failure
_ser_Node* _ser_nodeGetByTag(const char* tag, int tagLen) {
    for (int i = 0; i < globs.nodeCount; i++) {
        _ser_Node* s = &globs.nodes[i];
        if (s->depth != SER_ND_TOP) {
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
    _ser_nodePush(tag, strlen(tag), SER_NK_STRUCT, SER_ND_TOP);

    const char* c = str;
    while (true) {
        const char* name = NULL;
        int nameLen = 0;
        if (_ser_parseToken(str, &c, &name, &nameLen)) {
            bool isFirstKind = true;
            while (true) {
                const char* kind = NULL;
                int kindLen = 0;
                assert(_ser_parseToken(str, &c, &kind, &kindLen));

                _ser_NodeKind k = SER_NK_OTHER_USER;
                // start at one so that index values line up with enum values, and so that none is skipped
                for (int i = 1; i < sizeof(ser_nodeKindParsableNames) / sizeof(const char*); i++) {
                    if (strncmp(ser_nodeKindParsableNames[i], kind, kindLen) == 0) {
                        k = i;
                        break;
                    }
                }

                // NOTE: every node inside of one prop (the inner inside an array/ref) will also have a name
                _ser_Node* node = _ser_nodePush(name, nameLen, k, isFirstKind ? SER_ND_PROP : SER_ND_INNER);
                if (node->kind == SER_NK_OTHER_USER) {
                    _ser_Node* other = _ser_nodeGetByTag(kind, kindLen);
                    assert(other != NULL);
                    node->otherUserNode = other;
                }

                if (node->kind != SER_NK_ARRAY) {
                    break;  // end consuming kinds for this prop when reaching a terminal
                }
                isFirstKind = false;
            }  // loop to consume a number of kinds in case of compound things like arr and ref
        }  // end check for name
        else {
            break;
        }
    }  // end prop while
}

void _ser_specEnum(const char* tag, const char* strs[], int count) {
    _ser_Node* s = _ser_nodePush(tag, strlen(tag), SER_NK_ENUM, false);
    s->enumValues = strs;
    s->enumCount = count;
}

void _ser_specOffsets(const char* tag, int structSize, ...) {
    va_list args;
    va_start(args, structSize);

    _ser_Node* node = _ser_nodeGetByTag(tag, strlen(tag));
    assert(node != NULL);
    // while (true) {
    //     if () {

    //     }
    // }
    va_end(args);
}

#define _SER_STRINGIZE(x) #x
#define ser_specStruct(T, str) _ser_specStruct(#T, _SER_STRINGIZE(str))
#define ser_specEnum(T, strs, count) _ser_specEnum(#T, strs, count)

#define _SER_SELECT_BY_PARAM_COUNT(_1,_2,_3,NAME,...) NAME
#define FOO(...) GET_MACRO(__VA_ARGS__, FOO3, FOO2)(__VA_ARGS__)


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
        if (s->kind != SER_NK_OTHER_USER) {
            link = -1;
        }
        const char* indent = "";
        if (s->depth == SER_ND_PROP) {
            indent = "  ";
        }
        if (s->depth == SER_ND_INNER) {
            indent = "    ";
        }

        printf("%s%d: %-15.*s\tkind: ", indent, i, s->tagLen, s->tag);  // indent, index, tag
        if (s->kind == SER_NK_ENUM) {
            printf("enum\tvals: ");
            for (int valIndex = 0; valIndex < s->enumCount; valIndex++) {
                printf("\'%s\' ", s->enumValues[valIndex]);
            }
            printf("\n");
        } else if (s->kind == SER_NK_STRUCT) {
            printf("struct\n");
        } else {
            printf("%d\t\tother: %d\n", s->kind, link);
        }
    }
}
