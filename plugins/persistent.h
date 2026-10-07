#pragma once

/**
 * What the plugins keep across awl's restarts: ``key=value`` lines in one
 * file, at the config dictionary's ``state_home`` joined with its
 * ``awl_persistent`` (which the library's ``init()`` sets). The config
 * dictionary itself only lasts as long as awl.
 *
 * Plugin threads only: these read and write the file, and the main thread
 * never touches the file system. Calls are serialized among themselves.
 */

#include <stddef.h>
#include <stdint.h>

/** the config dictionary's key the file's path relative to ``state_home``
 * is under */
#define AWL_PERSISTENT_KEY "awl_persistent"

/** Writes the file's path to out. Returns 0, or -1 if ``state_home`` or
 * `AWL_PERSISTENT_KEY` isn't a string or the path doesn't fit. */
int awl_persistent_path( char* out, size_t n );
/** Reads key's number into v. Returns 0, or -1 if the file, the key or a
 * number there is missing. */
int awl_persistent_get_num( const char* key, int64_t* v );
/** Stores v under key, keeping the file's other lines, and creates the file
 * and its directories if need be. The new file replaces the old one whole
 * (rename()), so a crash leaves one or the other. Returns 0, or -1
 * (logged). */
int awl_persistent_set_num( const char* key, int64_t v );
