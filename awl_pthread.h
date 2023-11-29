#pragma once
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
} awl_pthread_start_routine_wrap_t;

static inline void* awl_pthread_wrap_start_routine( void* arg_ ) {
    awl_pthread_start_routine_wrap_t* w = (awl_pthread_start_routine_wrap_t*)arg_;
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
    awl_pthread_start_routine_wrap_t* _AWL_PTHREAD_ARG_W = calloc(1,sizeof(awl_pthread_start_routine_wrap_t)); \
    _AWL_PTHREAD_ARG_W->arg = ARG; \
    _AWL_PTHREAD_ARG_W->start_routine = START_ROUTINE; \
    sprintf( _AWL_PTHREAD_ARG_W->starter_location, "%s.%i", __FILE__, __LINE__ ); \
    pthread_create(THREAD, ATTR, awl_pthread_wrap_start_routine, _AWL_PTHREAD_ARG_W); \
}
