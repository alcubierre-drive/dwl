#include "logger.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void awl_logfunc_default_err( const char* pattern_, ... ) {
    va_list ap;
    va_start( ap, pattern_ );
    char pattern[2048] = "[ERR] ";
    strcat( pattern, pattern_ );
    vfprintf( stderr, pattern_, ap );
    va_end( ap );
}

static void awl_logfunc_default_wrn( const char* pattern_, ... ) {
    va_list ap;
    va_start( ap, pattern_ );
    char pattern[2048] = "[WRN] ";
    strcat( pattern, pattern_ );
    vfprintf( stderr, pattern_, ap );
    va_end( ap );
}

static void awl_logfunc_default_log( const char* pattern_, ... ) {
    va_list ap;
    va_start( ap, pattern_ );
    char pattern[2048] = "[LOG] ";
    strcat( pattern, pattern_ );
    vfprintf( stderr, pattern_, ap );
    va_end( ap );
}

static void awl_logfunc_default_vrb( const char* pattern_, ... ) {
    va_list ap;
    va_start( ap, pattern_ );
    char pattern[2048] = "[VRB] ";
    strcat( pattern, pattern_ );
    vfprintf( stderr, pattern_, ap );
    va_end( ap );
}

logfunc_t awl_logfunc_err = &awl_logfunc_default_err;
logfunc_t awl_logfunc_wrn = &awl_logfunc_default_wrn;
logfunc_t awl_logfunc_log = &awl_logfunc_default_log;
logfunc_t awl_logfunc_vrb = &awl_logfunc_default_vrb;

void awl_regsiter_logfunc( logfunc_t funcptr, awl_loglevel_t level ) {
    switch (level) {
        case awl_loglevel_err: awl_logfunc_err = funcptr; break;
        case awl_loglevel_wrn: awl_logfunc_wrn = funcptr; break;
        case awl_loglevel_log: awl_logfunc_log = funcptr; break;
        case awl_loglevel_vrb: awl_logfunc_vrb = funcptr; break;
        deafult: break;
    }
}
