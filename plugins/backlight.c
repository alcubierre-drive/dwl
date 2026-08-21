#include "backlight.h"
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>

/* vfork(2) is a glibc/BSD extension not declared under the strict
 * -D_POSIX_C_SOURCE=200809L this project is built with; see dwl.c's
 * spawn_pid() for the full explanation of why vfork() is used here rather
 * than popen()/fork(): this thread wakes up and forks once a second for
 * the lifetime of the process, in a program with several other threads,
 * so a fork()-based popen() is a frequent, ongoing chance to inherit a
 * lock (e.g. malloc's arena lock) held by another thread. */
extern pid_t vfork(void);

/* Runs argv (no shell), returns the first byte of its stdout, or -1 on
 * failure. Avoids popen(), which uses fork() internally. */
static int run_capture_first_byte(char *const argv[]) {
    int fds[2];
    if (pipe(fds) < 0)
        return -1;

    pid_t pid = vfork();
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        close(fds[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    if (pid < 0) {
        close(fds[0]);
        return -1;
    }

    char c = 0;
    ssize_t n = read(fds[0], &c, 1);
    /* drain any remaining output so the child never blocks on a full pipe */
    char buf[256];
    while (read(fds[0], buf, sizeof(buf)) > 0)
        ;
    close(fds[0]);
    waitpid(pid, NULL, 0);

    return n == 1 ? (unsigned char)c : -1;
}

static void* backlight( void* arg ) {
    if (!arg) return NULL;
    awl_backlight_t* b = arg;
    while (1) {
        sem_wait( &b->sem );
        char *args[] = {"systemctl", "--user", "is-active", "backlight-tooler.timer", NULL};
        int c = run_capture_first_byte(args);
        int enabled = (c == 'i') ? 0 : 1;
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
