#include "stats.h"
#include "bar.h"
#include "init.h"
#include "../awl_log.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static void* stats_thread_run( void* arg );

static float cpu_idle( uint64_t* sizes_table );
static void rotate_back( float* array, int size );
static void getmem( float* mem, float* swp );

awl_stats_t* start_stats_thread( int nval_cpu, int nval_mem, int nval_swp, int update_sec ) {
    P_awl_log_printf( "starting system monitor" );
    awl_stats_t* st = calloc(1,sizeof(awl_stats_t));
    st->ncpu = nval_cpu;
    st->nmem = nval_mem;
    st->nswp = nval_swp;
    st->update_sec = update_sec;
    st->sizes_table = calloc(20, sizeof(uint64_t));
    pthread_mutex_init( &st->mtx, NULL );
    pthread_create( &st->me, NULL, stats_thread_run, st );
    return st;
}

void stop_stats_thread( awl_stats_t* st ) {
    pthread_mutex_lock( &st->mtx );
    if (!pthread_cancel( st->me )) pthread_join( st->me, NULL );
    pthread_mutex_unlock( &st->mtx );
    pthread_mutex_destroy( &st->mtx );
    free( st->sizes_table );
    free( st );
}

static void* stats_thread_run( void* arg ) {
    awl_stats_t* st = (awl_stats_t*)arg;
    while (1) {
        pthread_mutex_lock( &st->mtx );
        rotate_back( st->cpu, st->ncpu );
        rotate_back( st->mem, st->nmem );
        rotate_back( st->swp, st->nswp );
        getmem( st->mem, st->swp );
        st->cpu[0] = 1. - cpu_idle(st->sizes_table);
        pthread_mutex_unlock( &st->mtx );
        sleep(st->update_sec);
    }
    return NULL;
}

static void rotate_back( float* array, int size ) {
    for (int i=size-1; i>=1; i--)
        array[i] = array[i-1];
}

static float cpu_idle( uint64_t* sizes_table ) {
    // copy old values
    memcpy( sizes_table+10, sizes_table, sizeof(uint64_t)*10 );
    // open file and read new values
    FILE* f = fopen("/proc/stat", "r");
    uint64_t* s = sizes_table;
    fscanf(f, "cpu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu", s+0, s+1, s+2, s+3, s+4, s+5, s+6, s+7, s+8, s+9);

    // calculate differences
    float diffs[10] = {0};
    for (int i=0; i<10; ++i) {
        diffs[i] = (s[i] - (s+10)[i]);
        if (i != 0) diffs[0] += diffs[i];
    }

    // find #cpus
    float ncpus = 0.0;
    ssize_t nread = 0;
    size_t len = 0;
    char* line = NULL;
    while ((nread = getline(&line, &len, f)) != -1) {
        int buf = 0;
        ncpus += sscanf(line, "cpu%d", &buf);
    }
    free(line);

    fclose(f);

    // return idle percentage
    if (ncpus < 1.0) ncpus = 1.0;
    #ifndef AWL_STATS_FORCE_CPU_MULT
    float result = diffs[3]/diffs[0];
    #else
    float result = diffs[3]/diffs[0] * ncpus;
    #endif
    return result > 0.0 ? (result < 1.0 ? result : 1.0) : 0.0;
}

static void getmem( float* mem, float* swp ) {
    FILE* f = fopen("/proc/meminfo", "r");
    long unsigned mem_total = 0,
                  mem_avail = 0,
                  swp_total = 0,
                  swp_free = 0;
    ssize_t nread = 0;
    size_t len = 0;
    char* line = NULL;
    while ((nread = getline(&line, &len, f)) != -1) {
        long unsigned buf = 0;
        if (mem_total == 0 && sscanf(line, "MemTotal: %lu kB", &buf))
            mem_total = buf;
        else if (mem_avail == 0 && sscanf(line, "MemAvailable: %lu kB", &buf))
            mem_avail = buf;
        else if (swp_total == 0 && sscanf(line, "SwapTotal: %lu kB", &buf))
            swp_total = buf;
        else if (swp_free == 0 && sscanf(line, "SwapFree: %lu kB", &buf))
            swp_free = buf;
    }
    free(line);
    fclose(f);
    if (mem_total == 0)
        *mem = 1.0;
    else
        *mem = 1.0 - (float)mem_avail/(float)mem_total;
    if (swp_total == 0)
        *swp = 1.0;
    else
        *swp = 1.0 - (float)swp_free/(float)swp_total;

    if (*mem < 0.0) *mem = 0.0;
    if (*mem > 1.0) *mem = 1.0;
    if (*swp < 0.0) *swp = 0.0;
    if (*swp > 1.0) *swp = 1.0;
}
