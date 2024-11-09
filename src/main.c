#include "snooze.h"
#include "PoolAlloc.h"
#include "csg.h"
#include "serialization2.h"
#include "sketches.h"

void main_init(snz_Arena* scratch) {
}

void main_frame(float dt, snz_Arena* scratch) {
}

int main() {
    _poolAllocTests();
    sk_tests();
    ser_tests();
    csg_tests();

    snz_main("CADDER V0.0", main_init, main_frame);

    return 0;
}