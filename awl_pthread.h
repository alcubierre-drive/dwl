#pragma once
#include <pthread.h>
#include <stdio.h>

int gettid(void);

typedef struct {
    char starter_location[64];
    void* (*start_routine)(void*);
    void* arg;
} awl_pthread_start_routine_wrap_t;

static inline void* awl_pthread_wrap_start_routine( void* arg_ ) {
    awl_pthread_start_routine_wrap_t* w = (awl_pthread_start_routine_wrap_t*)arg_;
    void* arg = w->arg;
    void* (*start_routine)(void*) = w->start_routine;
    printf( "%s: thread %i\n", w->starter_location, gettid() );
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
