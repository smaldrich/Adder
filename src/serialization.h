#pragma once
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
    SER_NK_PTR,

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
    "ptr",
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

    int offsetIntoStruct;

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

void _ser_printState() {
    // print out created nodes for debugging / testing
    printf("\nNODES!\n");
    for (int i = 0; i < globs.nodeCount; i++) {
        _ser_Node* n = &globs.nodes[i];

        if (n->depth == SER_ND_TOP) {
            printf("\n");
        }

        printf("%-3d:", i);
        int indent = 1;
        if (n->depth == SER_ND_PROP) {
            indent = 5;
        }
        if (n->depth == SER_ND_INNER) {
            indent = 7;
        }

        printf("%*s%-*.*s\tkind: ", indent, " ", 17 - indent, n->tagLen, n->tag);  // indent, index, tag
        if (n->kind == SER_NK_ENUM) {
            printf("enum\t\tvals: ");
            for (int valIndex = 0; valIndex < n->enumCount; valIndex++) {
                printf("\'%s\' ", n->enumValues[valIndex]);
            }
        } else if (n->kind == SER_NK_STRUCT) {
            printf("struct");
        } else if (n->kind == SER_NK_PTR) {
            printf("ptr");
        } else if (n->kind == SER_NK_ARRAY) {
            printf("arr");
        } else if (n->kind == SER_NK_OTHER_USER) {
            int link = (int)(n->otherUserNode - globs.nodes);
            printf("other\t\tindex: %d", link);
        } else {
            printf("%d", n->kind);
        }

        if (n->depth == SER_ND_PROP) {
            printf("\t\toffset in struct: %d", n->offsetIntoStruct);
        }
        printf("\n");
    }
}

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
        } else if (s->tagLen != tagLen) {
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

                if (node->kind != SER_NK_ARRAY && node->kind != SER_NK_PTR) {
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
    _ser_Node* s = _ser_nodePush(tag, strlen(tag), SER_NK_ENUM, SER_ND_TOP);
    s->enumValues = strs;
    s->enumCount = count;
}

void _ser_specOffsets(const char* tag, int structSize, ...) {
    va_list args;
    va_start(args, structSize);

    _ser_Node* node = _ser_nodeGetByTag(tag, strlen(tag));
    assert(node != NULL);
    assert(node->kind == SER_NK_STRUCT);

    int propIdx = (int)(node - globs.nodes);
    while (true) {
        propIdx++;
        if (propIdx >= globs.nodeCount) {
            break;
        }
        _ser_Node* prop = &globs.nodes[propIdx];
        if (prop->depth == SER_ND_TOP) {
            break;
        } else if (prop->depth == SER_ND_INNER) {
            continue;
        }

        int offset = va_arg(args, int);
        prop->offsetIntoStruct = offset;
    }

    va_end(args);
}

typedef enum {
    SERE_OK,
    SERE_FOPEN_FAILED
} ser_Error;

ser_Error ser_writeThings() {
    const char* path = "./testing/file1";
    FILE* fptr = fopen(path, "w");
    if (fptr == NULL) {
        printf("file open failed\n");
        return SERE_FOPEN_FAILED;
    }

    fprintf(fptr, "%s", "Hello to my wonderful new file!\n");
    fclose(fptr);

    return SERE_OK;
}

#define _SER_STRINGIZE(x) #x
#define ser_specStruct(T, str) _ser_specStruct(#T, _SER_STRINGIZE(str))
#define ser_specEnum(T, strs, count) _ser_specEnum(#T, strs, count)

#define _SER_OFFSET2(T, a) offsetof(T, a)
#define _SER_OFFSET3(T, a, b) offsetof(T, a), offsetof(T, b)
#define _SER_OFFSET4(T, a, b, c) offsetof(T, a), offsetof(T, b), offsetof(T, c)
#define _SER_OFFSET5(T, a, b, c, d) offsetof(T, a), offsetof(T, b), offsetof(T, c), offsetof(T, d)
#define _SER_OFFSET6(T, a, b, c, d, e) offsetof(T, a), offsetof(T, b), offsetof(T, c), offsetof(T, d), offsetof(T, e)
#define _SER_OFFSET7(T, a, b, c, d, e, f) offsetof(T, a), offsetof(T, b), offsetof(T, c), offsetof(T, d), offsetof(T, e), offsetof(T, f)
#define _SER_OFFSET8(T, a, b, c, d, e, f, g) offsetof(T, a), offsetof(T, b), offsetof(T, c), offsetof(T, d), offsetof(T, e), offsetof(T, f), offsetof(T, g)

#define _SER_SELECT_BY_PARAM_COUNT(_1,_2,_3,_4,_5,_6,_7,_8,NAME,...) NAME
#define _SER_OFFSET_SWITCH(...) _SER_SELECT_BY_PARAM_COUNT(__VA_ARGS__, _SER_OFFSET8, _SER_OFFSET7, _SER_OFFSET6, _SER_OFFSET5, _SER_OFFSET4, _SER_OFFSET3, _SER_OFFSET2)(__VA_ARGS__)
#define ser_offsets(T, ...) _ser_specOffsets(_SER_STRINGIZE(T), sizeof(T), _SER_OFFSET_SWITCH(T, __VA_ARGS__))

#include "HMM/HandmadeMath.h"
#include "sketches.h"

void ser_tests() {
    ser_specStruct(HMM_Vec2,
                   X float
                   Y float);
    ser_offsets(HMM_Vec2, X, Y);

    const char* lknames[] = { "straight", "arc" };
    ser_specEnum(sk_LineKind, lknames, sizeof(lknames) / sizeof(const char*));
    ser_specStruct(sk_Line,
                   kind   sk_LineKind
                   p1     ptr HMM_Vec2
                   p2     ptr HMM_Vec2
                   center ptr HMM_Vec2);
    ser_offsets(sk_Line, kind, p1, p2, center);

    const char* cknames[] = { "distance", "angleLines", "angleArc", "arcUniform", "axisAligned" };
    ser_specEnum(sk_ConstraintKind, cknames, sizeof(cknames) / sizeof(const char*));
    ser_specStruct(sk_Constraint,
                   kind  sk_ConstraintKind
                   line1 ptr sk_Line
                   line2 ptr sk_Line
                   value float);
    ser_offsets(sk_Constraint, kind, line1, line2, value);

    ser_specStruct(sk_Sketch,
                   points      arr HMM_Vec2
                   lines       arr sk_Line
                   constraints arr sk_Constraint);
    ser_offsets(sk_Sketch, points, lines, constraints);

    _ser_printState();

    ser_writeThings();
}