#include "stats.h"
#include "bar.h"
#include "../awl_log.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct {
    unsigned int i;
    float* cpu;
    int ncpu;
    float* mem;
    int nmem;
    float* swp;
    int nswp;
    int update_sec;
} th_stats_arg_t;

static uint64_t* sizes_table = NULL;

static pthread_t* th_stats = NULL;
static th_stats_arg_t* th_arg = NULL;
static void* th_stats_run( void* arg );
static int th_run = 0;
static float cpu_idle( void );
static void rotate_back( float* array, int size );
static void getmem( float* mem, float* swp );

void start_stats_thread( float* val_cpu, int nval_cpu,
                         float* val_mem, int nval_mem,
                         float* val_swp, int nval_swp, int update_sec ) {
    awl_log_printf( "starting system monitor" );
    th_arg = malloc(sizeof(th_stats_arg_t));
    th_arg->i = 0;
    th_arg->cpu = val_cpu;
    th_arg->ncpu = nval_cpu;
    th_arg->mem = val_mem;
    th_arg->nmem = nval_mem;
    th_arg->swp = val_swp;
    th_arg->nswp = nval_swp;
    th_arg->update_sec = update_sec;
    th_stats = malloc(sizeof(pthread_t));
    th_run = 1;
    pthread_create( th_stats, NULL, th_stats_run, th_arg );
}

void stop_stats_thread( void ) {
    th_run = 0;
    pthread_cancel( *th_stats );
    free( th_stats );
    free( th_arg );
    free( sizes_table );
    th_stats = NULL;
    th_arg = NULL;
}

static void* th_stats_run( void* arg ) {
    th_stats_arg_t* A = (th_stats_arg_t*)arg;
    while (th_run) {
        rotate_back( A->cpu, A->ncpu );
        rotate_back( A->mem, A->nmem );
        rotate_back( A->swp, A->nswp );
        getmem( th_arg->mem, th_arg->swp );
        th_arg->cpu[0] = 1. - cpu_idle();
        sleep(A->update_sec);
    }
    return NULL;
}

static void rotate_back( float* array, int size ) {
    for (int i=size-1; i>=1; i--)
        array[i] = array[i-1];
}

static float cpu_idle( void ) {
    if (!sizes_table)
        sizes_table = (uint64_t*)calloc(20, sizeof(uint64_t));

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
#ifdef AWL_STATS_SKIP_CPU_MULT
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
