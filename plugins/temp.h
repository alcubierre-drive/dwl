#pragma once

#include <stdint.h>
#include <semaphore.h>

typedef struct temp_thread_t temp_thread_t;

typedef struct awl_temperature_t {
    // output
    float temps[16];
    uint8_t idx[16];
    uint8_t ntemps;
    uint8_t ready;
    // input
    float f_t_max[16];
    float f_t_min[16];
    char f_files[16][256];
    char f_labels[16][16];
    uint8_t f_ntemps;

    temp_thread_t* handle;
    sem_t sem;
} awl_temperature_t;

void start_temp_thread( awl_temperature_t* t, int update_sec );
void stop_temp_thread( awl_temperature_t* t );

uint32_t temp_color( float T, float min, float max );
