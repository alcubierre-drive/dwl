#pragma once

#include <semaphore.h>
#include <stdint.h>

typedef struct awl_stats_t {
    float cpu[128], mem[128], swp[128];
    int ncpu, nmem, nswp;

    int dir;

    uint64_t* sizes_table;
    sem_t sem;
} awl_stats_t;

awl_stats_t* stats_init( int nval_cpu, int nval_mem, int nval_swp );
/* Shifts the graphs by one sample. Always changes what the bar shows. */
void stats_update( awl_stats_t* st );
void stats_free( awl_stats_t* st );
