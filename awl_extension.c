#include "awl_extension.h"
#include "awl_util.h"
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

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

    if (!handle->addr)
        die("could not open lib %s\n", lib);
    if (!handle->vt)
        die("could not read vtable (%s)\n", lib);

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
    union {
        awl_func_t r;
        uintptr_t u;
    } uresult;
    assert(sizeof(awl_func_t) == sizeof(void*));
    if (sizeof(uintptr_t) == sizeof(void*))
        uresult.u = (uintptr_t)dlsym( handle->addr, name );
    else
        memset(&uresult, 0, sizeof(uresult));
    return uresult.r;
}
