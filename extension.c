#include "extension.h"
#include "extension_callbacks.h"
#include <dlfcn.h>

typedef Key* (*keyfunc_t)( int* );
typedef void (*init_t)( callbacks_t* );

static extension_func_t extension_get_fun( const char* name );

static void* lib_extension = NULL;
static const char lib_name[] = "libdwlextend.so";
static callbacks_t callbacks = {0};

void extension_init( void ) {
    init_t i = NULL;
    lib_extension = dlopen( lib_name, RTLD_LAZY );
    callbacks = (callbacks_t){ .focusclient = focusclient,
                               .focustop = focustop,
                               .printstatus = printstatus,
                               .get_client = get_client,
                               .get_selmon = get_selmon,
                               .arrange = arrange,
                               .extension_call = extension_call,
                               .extension_reload = extension_reload,

                               .spawn = spawn,
                               .focusstack = focusstack,
                               .incnmaster = incnmaster,
                               .setmfact = setmfact,
                               .zoom = zoom,
                               .toggleview = toggleview,
                               .tag = tag,
                               .toggletag = toggletag,
                               .killclient = killclient,
                               .setlayout = setlayout,
                               .togglefloating = togglefloating,
                               .togglefullscreen = togglefullscreen,
                               .view = view,
                               .focusmon = focusmon,
                               .tagmon = tagmon,
                               .quit = quit, };
    if (!lib_extension) {
        fprintf(stderr, "dl error: %s\n", dlerror());
    } else {
        i = (init_t)dlsym( lib_extension, "EX_init" );
        if (i) i( &callbacks );
    }
}

void extension_close( void ) {
    dlclose(lib_extension);
}

void extension_reload( const Arg* a ) {
    (void)a;
    extension_close();
    extension_init();
}

extension_func_t extension_get_fun( const char* name ) {
    extension_func_t fun = NULL;
    char* error = NULL;
    fun = (extension_func_t)dlsym( lib_extension, name );
    error = dlerror();
    if (error != NULL) {
        fun = NULL;
        fprintf(stderr, "dl error: %s\n", error);
    }
    return fun;
}

void extension_call( const Arg* arg ) {
    extension_func_t fun = NULL;
    fun = extension_get_fun( arg->ex.name );
    if (fun)
        (*fun)( &callbacks, (const Arg*)&(arg->ex.uarg) );
}

Key* extension_keys( int* length ) {
    keyfunc_t k = (keyfunc_t)dlsym( lib_extension, "EX_keys" );
    if (k) {
        return k(length);
    } else {
        *length = 0;
        return NULL;
    }
}
