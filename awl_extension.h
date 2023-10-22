#pragma once

#include "awl_state.h"

#define AWL_VTABLE_SYM EX_vtable
#define AWL_VTABLE_NAME "EX_vtable"
#define AWL_PLUGIN_NAME "libawlplugin.so"

typedef struct awl_vtable_t awl_vtable_t;
typedef struct awl_extension_t awl_extension_t;

typedef int (*awl_func_t)(awl_config_t*);

struct awl_vtable_t {
    void (*init)(void);
    void (*free)(void);

    awl_config_t* (*config)(void);
};

awl_extension_t* awl_extension_init( const char* lib );
void awl_extension_free( awl_extension_t* handle );
void awl_extension_refresh( awl_extension_t* handle );

awl_vtable_t* awl_extension_vtable( awl_extension_t* handle );
awl_func_t awl_extension_func( awl_extension_t* handle, const char* name );

