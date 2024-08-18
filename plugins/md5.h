#pragma once
#include <stdint.h>

typedef struct {
    uint64_t u[2];
} u128t;

char* awl_md5sum_str( const char* buf, uint64_t size );
u128t awl_md5sum_int( const char* buf, uint64_t size );
