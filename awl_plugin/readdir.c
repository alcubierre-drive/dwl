#include "readdir.h"

#include <dirent.h> 
#include <stdio.h> 

static int cmpstringp(const void *p1, const void *p2) {
    return strcmp(*(const char **) p1, *(const char **) p2);
}

awl_dirent_t awl_dirent_create( const char* path ) {

    awl_dirent_t res = {0};
    for (int i=0; i<AWL_DIRENT_MAXTYPES; ++i)
        vector_init( res._v+i, sizeof(char*) );

    res._p = strdup(path);

    awl_dirent_update( &res );
    return res;

}

void awl_dirent_update( awl_dirent_t* pres ) {
    // resize vectors
    for (int i=0; i<AWL_DIRENT_MAXTYPES; ++i) {
        vector_resize( pres->_v+i, 0 );
    }

    DIR *d = opendir(pres->_p);
    struct dirent *dir = NULL;
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (strlen(dir->d_name) > 0) {
                if (strcmp(dir->d_name, ".") && strcmp(dir->d_name, "..")) {
                    int hidden = dir->d_name[0] == '.';
                    int broken = dir->d_type == DT_UNKNOWN;
                    int folder = dir->d_type == DT_DIR;

                    int push_idx = awl_dirent_type_f;
                    if (broken)
                        push_idx = hidden ? awl_dirent_type_b_h : awl_dirent_type_b;
                    else if (folder)
                        push_idx = hidden ? awl_dirent_type_d_h : awl_dirent_type_d;
                    else if (hidden)
                        push_idx = awl_dirent_type_f_h;

                    char* s = strdup(dir->d_name);
                    vector_push_back( pres->_v+push_idx, &s );
                }
            }
        }
        closedir(d);
    }

    // append NULL and sort
    char* s = NULL;
    for (int i=0; i<AWL_DIRENT_MAXTYPES; ++i) {
        vector_push_back( pres->_v+i, &s );
        pres->v[i] = vector_data( pres->_v+i );
        qsort( pres->v[i], vector_size( pres->_v+i )-1, sizeof(char*), cmpstringp );
    }
}

void awl_dirent_destroy( awl_dirent_t d ) {
    for (int i=0; i<AWL_DIRENT_MAXTYPES; ++i) {
        for (char** A = d.v[i]; *A; A++) free(*A);
        vector_destroy( d._v + i );
    }
    free( d._p );
}

/* int main() { */
/*     awl_dirent_t D = awl_dirent_create( "Desktop/" ); */
/*     printf( "FILES:\n" ); */
/*     for (char** A = D.v[awl_dirent_type_f]; *A; A++) printf( "%s\n", *A ); */
/*     printf( "DIRS:\n" ); */
/*     for (char** A = D.v[awl_dirent_type_d]; *A; A++) printf( "%s\n", *A ); */
/*     awl_dirent_destroy( D ); */
/* } */
