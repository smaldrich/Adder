
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    SER_SK_FLOAT,
    SER_SK_INT,
    SER_SK_CHAR,
    SER_SK_STRUCT,
} ser_SpecKind;

typedef struct ser_Spec ser_Spec;
struct ser_Spec {
    const char* name;
    ser_SpecKind kind;
    ser_Spec* nextSibling;
    ser_Spec* parent;

    ser_Spec* firstChild;
};

#define SER_MAX_SPECS 10000

typedef struct {
    ser_Spec specs[SER_MAX_SPECS];
    int specCount;
    ser_Spec* firstSpec;

    ser_Spec* currentSpec;
    bool addNextSpecAsChild;  // TODO: this is kinda messy
} ser_Globs;

ser_Globs globs;

ser_Spec* _ser_specPush(const char* name) {
    assert(globs.specCount < SER_MAX_SPECS);
    ser_Spec* s = &globs.specs[globs.specCount++];
    s->name = name;

    if (globs.addNextSpecAsChild) {
        assert(globs.currentSpec != NULL);
        assert(globs.currentSpec->firstChild == NULL);
        globs.currentSpec->firstChild = s;
        s->parent = globs.currentSpec;
        globs.addNextSpecAsChild = false;
    } else {
        if (globs.currentSpec) {
            globs.currentSpec->nextSibling = s;
            s->parent = globs.currentSpec->parent;
        } else {
            globs.firstSpec = s;
        }
    }
    globs.currentSpec = s;

    return s;
}

void ser_spec(const char* name, ser_SpecKind kind) {
    ser_Spec* s = _ser_specPush(name);
    s->kind = kind;
    assert(kind != SER_SK_STRUCT);
}

void _ser_specStructBegin(const char* name) {
    ser_Spec* s = _ser_specPush(name);
    s->kind = SER_SK_STRUCT;
    globs.addNextSpecAsChild = true;
}
void _ser_specStructEnd() {
    globs.currentSpec = globs.currentSpec->parent;
}
#define _ser_defer(begin, end) for (int _i_ = ((begin), 0); !_i_; _i_ += 1, (end))
#define ser_specStruct(name) _ser_defer(_ser_specStructBegin(name), _ser_specStructEnd())

void ser_serializeBegin() {}
void ser_feedFloat() {}
void ser_feedInt() {}
void ser_feedChar() {}
void ser_beginComposite() {}
void ser_endComposite() {}
void ser_serializeEnd() {}

void ser_tests() {
    ser_specStruct("my struct of things") {
        ser_spec("1st inner", SER_SK_INT);
        ser_spec("2nd inner", SER_SK_INT);
    }

    ser_spec("outer", SER_SK_CHAR);
}