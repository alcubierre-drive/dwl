#pragma once

#include "vector.h"

typedef enum {
    awl_dirent_type_f = 0,
    awl_dirent_type_f_h,
    awl_dirent_type_d,
    awl_dirent_type_d_h,
    awl_dirent_type_b,
    awl_dirent_type_b_h,
    AWL_DIRENT_MAXTYPES // DO NOT USE
} awl_dirent_type_t;

typedef struct awl_dirent {
    char* _p;
    vector_t _v[AWL_DIRENT_MAXTYPES];
    char** v[AWL_DIRENT_MAXTYPES];
} awl_dirent_t;

awl_dirent_t awl_dirent_create( const char* path );
void awl_dirent_update( awl_dirent_t* d );
void awl_dirent_destroy( awl_dirent_t d );
