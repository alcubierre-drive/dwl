#pragma once

#include <stdatomic.h>
#include <pulse/mainloop-api.h>

typedef struct awl_backlight_bus_t awl_backlight_bus_t;

typedef struct awl_backlight_t {
    atomic_int enabled;
    awl_backlight_bus_t* bus; /* loop thread only */
} awl_backlight_t;

/* Tracks the timer on the loop behind api (poller.h); NULL on failure. */
awl_backlight_t* backlight_init( pa_mainloop_api* api );
/* Only once the loop thread has stopped. No-op for NULL. */
void backlight_free( awl_backlight_t* b );
