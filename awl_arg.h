#pragma once

#include <stdint.h>

typedef union {
    int i;
    uint32_t ui;
    float f;
    const void *v;
} Arg;
