#pragma once

/* Picks and decodes the wallpaper: lists dir's *.png files, picks one and
 * decodes it (Wuffs, wuffs/). A thread does that, the main thread only asks
 * for it and takes the result (see desktop_panel.c on why the main thread
 * never touches the file system); dwl's background.c shows it. */

#include <stdint.h>

/* premultiplied ARGB8888 (DRM_FORMAT_ARGB8888), rows of stride bytes; one
 * malloc(), freed by whoever has it last */
typedef struct awl_image_t {
    int width, height, stride;
    uint8_t data[];
} awl_image_t;

/* dir is relative to $HOME unless absolute. The thread starts with the
 * wallpaper the index file names. Logs if it can't start. */
void awl_wallpaper_start( const char* dir );
/* Returns nonzero if the thread had to be left running (see thread.h). */
int awl_wallpaper_stop( void );
/* steps forward (negative: back) in the sorted list; main thread */
void awl_wallpaper_step( int steps );
/* any other one; main thread */
void awl_wallpaper_random( void );
/* where the list stood at the last change: the current index, its length
 * and the index the next random change goes to. 0 if not known yet. Main
 * thread. */
int awl_wallpaper_position( int* cur, int* n, int* rand_next );
/* the newest decoded wallpaper not taken yet, or NULL; the caller owns it.
 * awl_redraw_request() announces one. Main thread. */
awl_image_t* awl_wallpaper_take( void );
