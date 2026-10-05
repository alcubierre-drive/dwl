#pragma once

/* AWL_PTHREAD_CREATE(): pthread_create(), returning its result. Without
 * NDEBUG it also prints the new thread's id and where it was started. */

#include <pthread.h>

#ifndef NDEBUG

#include <stdio.h>
#include <stdlib.h>

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
    free(w);
    return (*start_routine)( arg );
}

static inline int _awl_pthread_create( pthread_t* thread, const pthread_attr_t* attr,
                                       void* (*start_routine)(void*), void* arg,
                                       const char* file, int line ) {
    _AWL_PTHREAD_START_ROUTINE_WRAPPER_T* w = calloc(1, sizeof(*w));
    if (!w) return pthread_create( thread, attr, start_routine, arg );
    w->arg = arg;
    w->start_routine = start_routine;
    snprintf( w->starter_location, sizeof(w->starter_location), "%s.%i", file, line );
    int err = pthread_create( thread, attr, _AWL_PTHREAD_WRAP_START_ROUTINE, w );
    if (err) free(w);
    return err;
}

#define AWL_PTHREAD_CREATE(THREAD, ATTR, START_ROUTINE, ARG) \
    _awl_pthread_create( THREAD, ATTR, START_ROUTINE, ARG, __FILE__, __LINE__ )

#else // !NDEBUG

#define AWL_PTHREAD_CREATE pthread_create

#endif // !NDEBUG
