#include "awl_extension.h"
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>

#define DLOPEN_MODE RTLD_GLOBAL|RTLD_NOW 
static const char libawlextend[] = AWL_PLUGIN_NAME;

struct awl_extension_t {
    void* addr;
    char* name;
    awl_vtable_t* vt;
};

awl_extension_t* awl_extension_init( const char* lib_ ) {
    const char* lib = lib_ ? lib_ : libawlextend;

    awl_extension_t* handle = calloc( 1, sizeof(awl_extension_t) );
    handle->name = calloc( 1, strlen(lib)+1 );
    strcpy( handle->name, lib );

    handle->addr = dlopen( handle->name, DLOPEN_MODE );
    handle->vt = dlsym( handle->addr, AWL_VTABLE_NAME );

    return handle;
}

void awl_extension_free( awl_extension_t* handle ) {
    dlclose( handle->addr );
    free( handle->name );
    free( handle );
}

void awl_extension_refresh( awl_extension_t* handle ) {
    dlclose( handle->addr );
    handle->addr = dlopen( handle->name, DLOPEN_MODE );
    handle->vt = dlsym( handle->addr, AWL_VTABLE_NAME );
}

awl_vtable_t* awl_extension_vtable( awl_extension_t* handle ) {
    return handle->vt;
}

awl_func_t awl_extension_func( awl_extension_t* handle, const char* name ) {
    return dlsym( handle->addr, name );
}
