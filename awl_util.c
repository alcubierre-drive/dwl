/* See LICENSE.dwm file for copyright and license details. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "awl_util.h"
#include "awl_log.h"

void die(const char *fmt, ...) {
    if (!fmt) exit(1);

    char error_str[2048] = {0};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(error_str, 1024, fmt, ap);
    va_end(ap);

    if (fmt[0] && fmt[strlen(fmt)-1] == ':') {
        strcat(error_str, " ");
        strcat(error_str, strerror(errno));
    }

    if (awl_log_has_init())
        awl_err_printf( "%s", error_str );
    fprintf( stderr, "%s\n", error_str );
    exit(1);
}

void * ecalloc(size_t nmemb, size_t size) {
    void *p;

    if (!(p = calloc(nmemb, size)))
        die("calloc:");
    return p;
}
