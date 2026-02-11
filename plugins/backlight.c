#include "backlight.h"
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>

static void* backlight( void* arg ) {
    if (!arg) return NULL;
    awl_backlight_t* b = arg;
    while (1) {
        sem_wait( &b->sem );
        FILE* fp = popen("systemctl --user is-active backlight-tooler.timer", "r" );
        int enabled = 1;
        if (fp) {
            char buf[256] = {0};
            while (fgets(buf, sizeof(buf)-1, fp)) {}
            pclose(fp);
            if (buf[0] == 'i') enabled = 0;
            else enabled = 1;
        }
        atomic_store( &b->enabled, enabled );
        sem_post( &b->sem );
        usleep( b->update_sec * 1.e6 );
    }
    return NULL;
}

awl_backlight_t* start_backlight_thread( float update_sec ) {
    awl_backlight_t* b = calloc(1, sizeof *b);
    b->update_sec = update_sec;
    atomic_store( &b->enabled, 1 );
    sem_init( &b->sem, 0, 1 );
    AWL_PTHREAD_CREATE( &b->me, NULL, backlight, b );
    return b;
}

void stop_backlight_thread( awl_backlight_t* b ) {
    sem_wait( &b->sem );
    if (!pthread_cancel(b->me)) pthread_join( b->me, NULL );
    sem_destroy( &b->sem );
    free(b);
}
