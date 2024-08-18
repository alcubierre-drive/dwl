#pragma once

typedef void (*logfunc_t)( const char*, ... );
typedef enum {
    awl_loglevel_err = -1,
    awl_loglevel_wrn,
    awl_loglevel_log,
    awl_loglevel_vrb,
} awl_loglevel_t;

void awl_regsiter_logfunc( logfunc_t funcptr, awl_loglevel_t level );

extern logfunc_t awl_logfunc_err;
extern logfunc_t awl_logfunc_wrn;
extern logfunc_t awl_logfunc_log;
extern logfunc_t awl_logfunc_vrb;
