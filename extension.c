#include "extension.h"
#include "extension_callbacks.h"
#include <dlfcn.h>

typedef Key* (*keyfunc_t)( int* );
typedef Layout* (*layoutfunc_t)( int* );
typedef void (*init_t)( callbacks_t* );
typedef void (*finalize_t)( void );

Layout* layouts = NULL;
int n_layouts = 0;

static Layout* extension_layout( int* length );

static extension_call_t extension_get_fun( const char* name );

static void* lib_extension = NULL;
static const char lib_name[] = "libawlplugin.so";
static const char LOGfile[] = "LOG.txt";
static FILE* LOG_f = NULL;
static callbacks_t callbacks = {0};

static void LOG( const char* fmt, ... ) {
    if (LOG_f == NULL)
        LOG_f = fopen(LOGfile, "a");
    va_list ap;
    va_start(ap, fmt);
    vfprintf( LOG_f, fmt, ap );
    va_end(ap);
    fflush( LOG_f );
}

void extension_init( void ) {
    init_t init = NULL;
    LOG("dl opening\n");
    if (dlopen( lib_name, RTLD_NOLOAD ))
        LOG("dl already present\n");
    lib_extension = dlopen( lib_name, RTLD_NOW );
    callbacks = (callbacks_t){ .focusclient = focusclient,
                               .focustop = focustop,
                               .printstatus = printstatus,
                               .get_client = get_client,
                               .get_selmon = get_selmon,

                               .arrange = arrange,
                               .monocle = monocle,
                               .tile = tile,

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
        LOG("dl error: %s\n", dlerror());
    } else {
        LOG("dl init\n");
        init = (init_t)dlsym( lib_extension, "EX_init" );
        if (init) init( &callbacks );

        layouts = extension_layout(&n_layouts);
    }
}

void extension_close( void ) {
    finalize_t finalize = NULL;
    LOG("dl closing\n");
    finalize = (finalize_t)dlsym( lib_extension, "EX_finalize" );
    if (finalize) finalize();
    if (dlclose(lib_extension))
        LOG("dl error: %s\n", dlerror());
}

extension_call_t extension_get_fun( const char* name ) {
    extension_call_t fun = NULL;
    char* error = NULL;
    fun = (extension_call_t)dlsym( lib_extension, name );
    error = dlerror();
    if (error != NULL) {
        fun = NULL;
        LOG("dl error: %s\n", error);
    }
    return fun;
}

Key* extension_keys( int* length ) {
    keyfunc_t k = (keyfunc_t)dlsym( lib_extension, "EX_keys" );
    if (k) {
        LOG("dl keys\n");
        return k(length);
    } else {
        *length = 0;
        return NULL;
    }
}

static Layout* extension_layout( int* length ) {
    layoutfunc_t k = (layoutfunc_t)dlsym( lib_extension, "EX_layouts" );
    if (k) {
        LOG("dl layouts\n");
        return k(length);
    } else {
        *length = 0;
        return NULL;
    }
}
