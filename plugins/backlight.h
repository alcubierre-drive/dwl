#pragma once

#include <stdatomic.h>
#include <pulse/mainloop-api.h>

typedef struct awl_backlight_bus_t awl_backlight_bus_t;

/** The backlight-tooler daemon, as seen on the user bus (backlight.c). */
typedef struct awl_backlight_t {
    /** whether the daemon runs (owns ``org.BacklightTooler``) */
    atomic_int running;
    /** whether it runs and updates automatically (``Interval`` > 0) */
    atomic_int enabled;
    /** screen brightness (``ScreenBrightness``) in per mille, -1 if unknown */
    atomic_int screen;
    /** last webcam reading (``WebcamValue``) in per mille, -1 if unknown or
     * none yet */
    atomic_int webcam;
    /** loop thread only */
    awl_backlight_bus_t* bus;
} awl_backlight_t;

/** Tracks the daemon on the loop behind api (poller.h); NULL on failure. */
awl_backlight_t* backlight_init( pa_mainloop_api* api );
/** Only once the loop thread has stopped. No-op for NULL. */
void backlight_free( awl_backlight_t* b );
