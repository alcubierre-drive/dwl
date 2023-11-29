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

static inline void* _AWL_PTHREAD_WRAP_START_ROUTINE( void* arg_ ) {
    _AWL_PTHREAD_START_ROUTINE_WRAPPER_T* w = (_AWL_PTHREAD_START_ROUTINE_WRAPPER_T*)arg_;
    void* arg = w->arg;
    void* (*start_routine)(void*) = w->start_routine;
    printf( "%s: thread %i\n", w->starter_location, gettid() );
    char nam[16] = {0};
    for (char* c=w->starter_location; *c; ++c)
        if (*c == '/') strncpy( nam, c+1, 15 );
    if (!nam[0]) strncpy( nam, w->starter_location, 15 );
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
