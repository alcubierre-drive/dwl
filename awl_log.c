#include "awl_log.h"
#include "awl_util.h"
#include <stdio.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>

static FILE* awllg = NULL;
static int awllg_lvl = -1;

static void create_dir( const char* path );

/* $XDG_CACHE_HOME defines the base directory relative to which user-specific
 * non-essential data files should be stored. If $XDG_CACHE_HOME is either not
 * set or empty, a default equal to $HOME/.cache should be used. */
void awl_log_init( int level ) {
    static char logfile_basename[] = "awl.log";
    char logfile[PATH_MAX] = {0};
    char* cachedir = getenv( "XDG_CACHE_HOME" );
    if (cachedir) {
        strcpy( logfile, cachedir );
        create_dir( logfile );
    } else {
        char* home = getenv( "HOME" );
        if (!home)
            die( "could not read home directory" );
        strcpy( logfile, home );
        strcat( logfile, "/.cache" );
        create_dir( logfile );
    }
    strcat( logfile, "/" );
    strcat( logfile, logfile_basename );

    if (access(logfile, F_OK|W_OK|R_OK) == 0) {
        char olglogfile[PATH_MAX] = {0};
        strcpy(olglogfile, logfile);
        strcat(olglogfile, ".old");
        rename(logfile, olglogfile);
    }

    awllg = fopen(logfile, "w");
    if (!awllg) die("could not open file %s", logfile);
    awllg_lvl = level;
}

void awl_log_destroy( void ) {
    if (awllg) fclose(awllg);
    awllg = NULL;
    awllg_lvl = -1;
}

int awl_log_has_init( void ) {
    return (awllg != NULL) && (awllg_lvl != -1);
}

void awl_log_printer_( const char* logname, int loglevel, const char* fname, int line, const char* fmt, ... ) {
    if (loglevel > awllg_lvl) return;
    if (!awllg) return;

    time_t t;
    time(&t);
    struct tm* lt = localtime(&t);
    char prefix[1024] = {0};
    strftime( prefix, 128, "%F %T ", lt );
    char* p = prefix + strlen(prefix);
    char logname_upper[32] = {0};
    strcpy(logname_upper, logname);
    for (char* l=logname_upper; *l; ++l) *l = toupper(*l);
    sprintf(p, "[%s %s:%i] ", logname_upper, fname, line);

    fprintf(awllg, "%s", prefix);
    va_list ap;
    va_start( ap, fmt );
    vfprintf( awllg, fmt, ap );
    va_end( ap );
    fputc( '\n', awllg );

    fflush(awllg);
}

static void create_dir( const char* path ) {
    if (mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO) == -1) {
        if (errno == EEXIST) {
            struct stat st = {0};
            if (stat(path, &st) == -1)
                die( "could not stat directory %s:", path );
            if ((st.st_mode & S_IFMT) != S_IFDIR)
                die( "file %s exists and is not a directory", path );
        } else
            die( "error creating directory %s:", path );
    }
}

