#include <stdbool.h>
#include <stdio.h>

void test_print(bool result, const char* name) {
    const char* colorCode = (result) ? "\x1B[0;32m" : "\x1B[0;31m";
    const char* resultStr = (result) ? "passed" : "failed";
    printf("\x1B[0m[Test] %s\"%s\" %s\x1B[0m\n", colorCode, name, resultStr);
}
