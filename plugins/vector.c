#include "vector.h"

#ifndef VECTOR_DEFAULT_CAPACITY
#define VECTOR_DEFAULT_CAPACITY 16
#endif

vector_t vector_init( vector_t* place, int64_t elem_size ) {
    vector_t result = {
        .data = NULL,
        .elem_size = elem_size,
        .max_n_elems = 0,
        .cur_n_elems = 0,
    };
    vector_reserve( &result, VECTOR_DEFAULT_CAPACITY );
    if (place) *place = result;
    return result;
}

void vector_resize( vector_t* vec, int64_t sz ) {
    vector_reserve( vec, sz );
    vec->cur_n_elems = sz;
}

void vector_reserve( vector_t* vec, int64_t sz ) {
    if (sz >= vec->max_n_elems) {
        vec->max_n_elems = sz;
        vec->data = realloc( vec->data, vec->max_n_elems*vec->elem_size );
    }
}

void vector_push_back( vector_t* vec, const void* elem ) {
    vec->cur_n_elems++;
    if (vec->max_n_elems < vec->cur_n_elems) {
        vec->max_n_elems = vec->cur_n_elems * 2;
        vec->data = realloc( vec->data, vec->max_n_elems*vec->elem_size );
    }
    memcpy( vec->data+(vec->cur_n_elems-1)*vec->elem_size, elem, vec->elem_size );
}

void vector_pop_back( vector_t* vec, void* elem ) {
    if (vec->cur_n_elems > 0) {
        unsigned char* pop = vec->data + (--vec->cur_n_elems)*vec->elem_size;
        if (elem) memcpy( elem, pop, vec->elem_size );
    }
}

void vector_get( vector_t* vec, int64_t idx, void* elem ) {
    memcpy( elem, vec->data + vec->elem_size*idx, vec->elem_size );
}

void vector_set( vector_t* vec, int64_t idx, const void* elem ) {
    memcpy( vec->data + vec->elem_size*idx, elem, vec->elem_size );
}

int64_t vector_size( vector_t* vec ) {
    return vec->cur_n_elems;
}

void* vector_data( vector_t* vec ) {
    return (void*)vec->data;
}

void vector_destroy( vector_t* vec ) {
    free( vec->data );
    memset( vec, 0, sizeof(vector_t) );
}

