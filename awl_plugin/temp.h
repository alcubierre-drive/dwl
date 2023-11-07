#pragma once

typedef struct awl_temperature_t {
    // output
    float temps[16];
    int idx[16];
    int ntemps;
    int ready;
    // input
    float f_limits[16];
    char f_files[16][256];
    char f_labels[16][16];
    int f_ntemps;
} awl_temperature_t;

void start_temp_thread( awl_temperature_t* t, int update_sec );
void stop_temp_thread( void );
