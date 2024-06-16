
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    SER_SK_FLOAT,
    SER_SK_INT,
    SER_SK_CHAR,
    SER_SK_ENUM,
    SER_SK_COMPOSITE,
} ser_SpecKind;

typedef struct ser_Spec ser_Spec;
struct ser_Spec {
    bool isTopLevel;
    const char* name;
    ser_SpecKind kind;

    ser_Spec* nextSibling;
};

#define SER_MAX_SPECS 10000
typedef struct {
    ser_Spec specs[SER_MAX_SPECS];
    int specCount;
} ser_Globs;
ser_Globs globs;

ser_Spec* _ser_specPush(const char* name, bool asSibling) {
    assert(globs.specCount < SER_MAX_SPECS);
    ser_Spec* s = &globs.specs[globs.specCount++];
    s->name = name;
    return s;
}

void _ser_newSpecStruct(const char* tag, const char* str) {
}
#define ser_newSpecStruct(T, str) _ser_newSpecStruct(#T, str)

void _ser_newSpecEnum(const char* tag, const char* strs[]) {
}
#define ser_newSpecEnum(T, str) _ser_newSpecEnum(#T, str)

#define _ser_defer(begin, end) for (int _i_ = ((begin), 0); !_i_; _i_ += 1, (end))

void ser_tests() {
    ser_newSpecStruct(HMM_Vec2,
                      "X float "
                      "Y float ");

    const char* lknames[] = {"straight", "arc"};
    ser_newSpecEnum("sk_ser_lineKind", lknames);
    ser_newSpecStruct(sk_Line,
                      "kind   sk_ser_LineKind "
                      "p1     ref HMM_Vec2 "
                      "p2     ref HMM_Vec2 "
                      "center ref HMM_Vec2 ");

    const char* cknames[] = {"distance", "angleLines", "angleArc", "arcUniform", "axisAligned"};
    ser_newSpecEnum(sk_ConstraintKind, cknames);
    ser_newSpecStruct(sk_Constraint,
                      "kind  sk_ser_ConstraintKind "
                      "line1 ref sk_ser_Line "
                      "line2 ref sk_ser_Line "
                      "value float ");

    ser_newSpecStruct(sk_Sketch,
                      "points      arr HMM_Vec2 "
                      "lines       arr sk_ser_Line "
                      "constraints arr sk_ser_Constraint ");
}
