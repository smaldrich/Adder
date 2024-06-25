#define BASE_IMPL
#include "base/allocators.h"

#include "serialization2.h"
#include "sketches.h"
#include "ui.h"

int main() {
    printf("\n");
    sk_tests();
    ser_tests();
    ui_tests();
}