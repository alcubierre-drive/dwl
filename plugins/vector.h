#pragma once

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>

struct vector {
    unsigned char* data;
    int64_t elem_size;
    int64_t max_n_elems, cur_n_elems;
};
typedef struct vector vector_t;

vector_t vector_init( vector_t* place, int64_t elem_size );
void vector_destroy( vector_t* vec );

int64_t vector_size( vector_t* vec );
void* vector_data( vector_t* vec );

void vector_reserve( vector_t* vec, int64_t sz );
void vector_resize( vector_t* vec, int64_t sz );

void vector_push_back( vector_t* vec, const void* elem );
void vector_pop_back( vector_t* vec, void* elem );
void vector_get( vector_t* vec, int64_t idx, void* elem );
void vector_set( vector_t* vec, int64_t idx, const void* elem );
