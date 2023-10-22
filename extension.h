#pragma once

#include <stdint.h>

typedef struct Client Client;
typedef struct Monitor Monitor;
typedef struct Layout Layout;

extern Layout* layouts;
extern int n_layouts;

typedef struct Key Key;

Key* extension_keys( int* length );
void extension_init( void );
void extension_close( void );

