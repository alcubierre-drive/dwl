#pragma once

void start_stats_thread( float* val_cpu, int nval_cpu,
                         float* val_mem, int nval_mem,
                         float* val_swp, int nval_swp, int update_sec );
void stop_stats_thread( void );
