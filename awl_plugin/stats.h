#pragma once

#include "../awl_pthread.h"
#include <semaphore.h>
#include <stdint.h>

typedef struct awl_stats_t {
    float cpu[128], mem[128], swp[128];
    int ncpu, nmem, nswp;

    int dir;

    pthread_t me;
    int update_sec;
    uint64_t* sizes_table;
    sem_t sem;
} awl_stats_t;

awl_stats_t* start_stats_thread( int nval_cpu, int nval_mem, int nval_swp, int update_sec );
void stop_stats_thread( awl_stats_t* st );
