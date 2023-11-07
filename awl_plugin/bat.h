#pragma once

typedef struct awl_battery_t {
    float charge;
    int charging;
    // -1: invalid
} awl_battery_t;

void start_bat_thread( awl_battery_t* b, int update_sec );
void stop_bat_thread( void );
