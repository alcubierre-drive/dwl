#pragma once

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>

#include <pixman-1/pixman.h>

static void dbg_printf( const char* file, const char* func, int line, const char* fmt, ... ) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    char tstr[128];
    sprintf(tstr, "%ld %09ld", t.tv_sec, t.tv_nsec);
    char xstr[1024];
    sprintf( xstr, "%s %s %s@%i: ", tstr, file, func, line );
    char fstr[1024];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(fstr, fmt, ap);
    va_end(ap);
    fprintf( stderr, "%s%s\n", xstr, fstr );
}
#define DBG_MSG( ... ) dbg_printf( __FILE__, __func__, __LINE__, __VA_ARGS__ )

static const pixman_color_t white = {.red = 0xAAAA, .green = 0xAAAA, .blue = 0xAAAA, .alpha = 0xAAAA};
static const pixman_color_t black = {.red = 0x0000, .green = 0x0000, .blue = 0x0000, .alpha = 0xFFFF};

pixman_color_t color_8bit_to_16bit( uint32_t c );
pixman_color_t alpha_blend_16( pixman_color_t B, pixman_color_t A );

#define COLOR_16BIT_QUICK( R, G, B, A ) { \
    .red = 0x##R##R, .green = 0x##G##G, .blue = 0x##B##B, .alpha = 0x##A##A, \
}
#define MAX( A, B ) ( (A) > (B) ? (A) : (B) )

enum pointer_event_mask {
    POINTER_EVENT_ENTER = 1 << 0,
    POINTER_EVENT_LEAVE = 1 << 1,
    POINTER_EVENT_MOTION = 1 << 2,
    POINTER_EVENT_BUTTON = 1 << 3,
    POINTER_EVENT_AXIS = 1 << 4,
    POINTER_EVENT_AXIS_SOURCE = 1 << 5,
    POINTER_EVENT_AXIS_STOP = 1 << 6,
    POINTER_EVENT_AXIS_DISCRETE = 1 << 7,
};

