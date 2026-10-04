#pragma once

#include <stddef.h>
#include <stdint.h>
#include <semaphore.h>

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

    sem_t sem;
} awl_temperature_t;

/* The caller fills in the input fields, then calls temp_init(). */
void temp_init( awl_temperature_t* t );
/* Finds the tempN_input of the hwmon device called `name` whose label starts
 * with `label`; writes its path to `out` and returns nonzero on success. */
int temp_find_hwmon( const char* name, const char* label, char* out, size_t n );
/* Re-reads all sensors; returns nonzero if a reading changed. */
int temp_update( awl_temperature_t* t );
void temp_fini( awl_temperature_t* t );

uint32_t temp_color( float T, float min, float max );
