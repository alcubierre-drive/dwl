#pragma once

#include <stdatomic.h>
#include <stdint.h>

#define AWL_STATS_MAX 128

/* The graphs are ring buffers, read with stats_sample(): the newest sample
 * of a graph with n samples is at index samples % n. It is stored before
 * `samples` moves on, so a reader racing an update can at most see the
 * oldest sample replaced by the newest. */
typedef struct awl_stats_t {
    _Atomic float cpu[AWL_STATS_MAX], mem[AWL_STATS_MAX], swp[AWL_STATS_MAX];
    atomic_uint samples; // number of updates so far
    int ncpu, nmem, nswp; // set by stats_init(), then read-only

    uint64_t* sizes_table; // poller thread only
} awl_stats_t;

awl_stats_t* stats_init( int nval_cpu, int nval_mem, int nval_swp );
/* Adds a sample to each graph. Always changes what the bar shows. */
void stats_update( awl_stats_t* st );
void stats_free( awl_stats_t* st );

/* Sample i (0: newest) of the graph g with n samples, at head `samples`
 * (one atomic_load() of st->samples, shared by all reads of a frame). */
static inline float stats_sample( _Atomic float* g, int n, unsigned samples, int i ) {
    return atomic_load( &g[(samples % n + n - i) % n] );
}
