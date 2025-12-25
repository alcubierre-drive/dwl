#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>

typedef struct {
    int opened;
    int fd;
    size_t maxsize;
    size_t currsize;
    char fname[1024];
} logfile_t;

static logfile_t logfile = {
    .opened = 0,
    .fd = -1,
    .maxsize = 1024*128,
    .currsize = 0,
    .fname = "/tmp/dwl.log",
};

static void logfile_close( void ) { if (logfile.fd > 0) close(logfile.fd); }

void logprintf( const char* fmt, ... ) {
    if (logfile.opened == 0) {
        logfile.fd = open(logfile.fname, O_RDWR | O_CREAT, 0644);
        if (logfile.fd > 0) {
            ftruncate(logfile.fd, 0);
            logfile.opened = 1;
            atexit( &logfile_close );
        } else {
            logfile.opened = -1;
        }
    }
    if (logfile.opened == -1) return;

    char line[1024] = {0};
    va_list ap;
    va_start(ap, fmt);
    size_t len = vsnprintf(line, sizeof(line)-1, fmt, ap);
    va_end(ap);

    logfile.currsize += len;
    if (logfile.currsize >= logfile.maxsize) ftruncate(logfile.fd, (logfile.currsize=0));
    write(logfile.fd, line, len);
    fsync(logfile.fd);
}
