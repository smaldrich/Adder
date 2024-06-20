#define BASE_IMPL
#include "base/allocators.h"

#include "serialization2.h"
#include "sketches.h"

int main() {
    printf("\n");
    sk_tests();
    ser_tests();
}