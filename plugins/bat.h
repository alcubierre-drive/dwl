#pragma once

typedef struct awl_battery_t {
    _Atomic float charge;
    _Atomic int charging;
    // -1: invalid

    int uevent_fd; // kernel uevents (netlink), -1 if unavailable
} awl_battery_t;

awl_battery_t* bat_init( void );
/* Re-reads the battery; returns nonzero if charge or state changed. */
int bat_update( awl_battery_t* b );
/* Call when uevent_fd is readable. Drains it and re-reads the battery if any
 * power_supply device (battery or AC adapter) changed; returns like
 * bat_update(). */
int bat_dispatch( awl_battery_t* b );
void bat_free( awl_battery_t* b );
