#pragma once

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/** a reading below absolute zero: the sensor can't be read (not NaN, which
 * ``-Ofast`` assumes never happens) */
#define AWL_TEMP_NONE -1000.0f

/** Up to 16 temperature sensors. */
typedef struct awl_temperature_t {
    /** output: reading of sensor i in °C, `AWL_TEMP_NONE` if it can't be read */
    _Atomic float temps[16];
    /** input, read-only once `temp_init()` was called: the bar's color
     * scale, from f_t_min to f_t_max */
    float f_t_max[16];
    float f_t_min[16];
    char f_files[16][256];
    char f_labels[16][16];
    uint8_t f_ntemps;
} awl_temperature_t;

/** The caller fills in the input fields, then calls `temp_init()`. */
void temp_init( awl_temperature_t* t );
/** Finds the ``tempN_input`` of the hwmon device called ``name`` whose label
 * starts with ``label``; writes its path to ``out`` and returns nonzero on
 * success. */
int temp_find_hwmon( const char* name, const char* label, char* out, size_t n );
/** Re-reads all sensors; returns nonzero if a reading changed. */
int temp_update( awl_temperature_t* t );

/** The bar's color (0xRRGGBBAA) for temperature T on the scale min..max. */
uint32_t temp_color( float T, float min, float max );
