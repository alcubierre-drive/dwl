#pragma once

#include <stdint.h>

typedef struct Client Client;
typedef struct Monitor Monitor;

typedef struct extension_arg_t {
    char name[128];
    union {
        int i;
        uint32_t ui;
        void* v;
        float f;
    } uarg;
} extension_arg_t;

typedef union {
    int i;
    uint32_t ui;
    float f;
    const void *v;
    extension_arg_t ex;
} Arg;

typedef struct Key Key;

Key* extension_keys( int* length );
void extension_init( void );
void extension_reload( const Arg* arg );
void extension_close( void );

void extension_call( const Arg* arg );
