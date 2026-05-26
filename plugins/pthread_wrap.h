#pragma once

#ifndef NDEBUG

#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int gettid(void);

typedef struct {
    char starter_location[32];
    void* (*start_routine)(void*);
    void* arg;
} _AWL_PTHREAD_START_ROUTINE_WRAPPER_T;

#ifndef MIN
#define MIN(x,y) (((x) < (y)) ? (x) : (y))
#endif

static inline void strxcpy( char* out, const char* in, size_t maxlen ) {
    size_t sz = strlen(in);
    sz = MIN(sz,maxlen-1);
    memcpy( out, in, sz );
    out[maxlen-1] = 0;
}


static inline void* _AWL_PTHREAD_WRAP_START_ROUTINE( void* arg_ ) {
    _AWL_PTHREAD_START_ROUTINE_WRAPPER_T* w = (_AWL_PTHREAD_START_ROUTINE_WRAPPER_T*)arg_;
    void* arg = w->arg;
    void* (*start_routine)(void*) = w->start_routine;
    printf( "%s: thread %i\n", w->starter_location, gettid() );
    char nam[32] = {0};
    for (char* c=w->starter_location; *c; ++c)
        if (*c == '/') strxcpy( nam, c+1, 32 );
    if (!nam[0]) strxcpy( nam, w->starter_location, 32 );
    /* pthread_setname_np( pthread_self(), nam ); */
    free(w);
    return (*start_routine)( arg );
}

#define AWL_PTHREAD_CREATE(THREAD, ATTR, START_ROUTINE, ARG) { \
    _AWL_PTHREAD_START_ROUTINE_WRAPPER_T* _AWL_PTHREAD_ARG_W = calloc(1,sizeof(_AWL_PTHREAD_START_ROUTINE_WRAPPER_T)); \
    _AWL_PTHREAD_ARG_W->arg = ARG; \
    _AWL_PTHREAD_ARG_W->start_routine = START_ROUTINE; \
    sprintf( _AWL_PTHREAD_ARG_W->starter_location, "%s.%i", __FILE__, __LINE__ ); \
    pthread_create(THREAD, ATTR, _AWL_PTHREAD_WRAP_START_ROUTINE, _AWL_PTHREAD_ARG_W); \
}

#else // !NDEBUG

#include <pthread.h>
#define AWL_PTHREAD_CREATE pthread_create

#endif // !NDEBUG
