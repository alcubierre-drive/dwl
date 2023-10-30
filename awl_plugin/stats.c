#include "stats.h"
#include "bar.h"
#include <pthread.h>
#include <sys/sysinfo.h>
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

static pthread_t* th_stats = NULL;
static th_stats_arg_t* th_arg = NULL;
static void* th_stats_run( void* arg );
static int th_run = 0;
static float cpu_idle( void );
static void rotate_back( float* array, int size );

void start_stats_thread( float* val_cpu, int nval_cpu,
                         float* val_mem, int nval_mem,
                         float* val_swp, int nval_swp, int update_sec ) {
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
    th_stats = NULL;
    th_arg = NULL;
}

static void* th_stats_run( void* arg ) {
    th_stats_arg_t* A = (th_stats_arg_t*)arg;
    while (th_run) {
        struct sysinfo info;
        rotate_back( A->cpu, A->ncpu );
        rotate_back( A->mem, A->nmem );
        rotate_back( A->swp, A->nswp );
        if (!sysinfo(&info)) {
            th_arg->mem[0] = 1. - (float)info.freeram/(float)info.totalram;
            th_arg->swp[0] = 1. - (float)info.freeswap/(float)info.totalswap;
            th_arg->cpu[0] = 1. - cpu_idle();
        }
        /* awl_bar_refresh(); */
        sleep(A->update_sec);
    }
    return NULL;
}

static void rotate_back( float* array, int size ) {
    for (int i=size-1; i>=1; i--)
        array[i] = array[i-1];
}

// TODO this is broken
static float cpu_idle( void ) {
    FILE* f = fopen("/proc/stat", "r");
    long unsigned sizes[10] = {0};
    fscanf(f, "cpu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu", sizes+0, sizes+1,
            sizes+2, sizes+3, sizes+4, sizes+5, sizes+6, sizes+7, sizes+8, sizes+9);
    fclose(f);
    float total_time = 0.0;
    for (int i=0; i<10; ++i) total_time += sizes[i];
    return (float)sizes[3] / total_time;
}
