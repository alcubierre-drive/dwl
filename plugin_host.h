#pragma once

/* dwl's side of libawlplugins.so (see awl_plugin_abi.h): loads it, swaps it
 * for a freshly built one on reload, and forwards to it. Main thread only.
 *
 * The library is looked up next to the dwl binary, or at $AWL_PLUGINS if set.
 * It is copied into a memfd before dlopen(), so rebuilding it while dwl runs
 * is safe and every load gets the current file, never a cached handle. */

#include "drwl.h"

/* Loads the library and starts the plugins. Returns 0 on success; on
 * failure dwl runs on with empty bars until a reload succeeds. */
int awl_plugins_load( int paused );

/* Loads the library from disk again and swaps it in. detach() must clear
 * every bar's widgets and drop any pointer to them (hover state); it runs
 * while the old library is still loaded, so widget callbacks may still be
 * called from it. attach() must give every bar its widgets again via
 * awl_plugins_bar_widgets(). If the new library doesn't load, the old one
 * stays and its plugins are just restarted. Returns 0 if the new library
 * was swapped in. */
int awl_plugins_reload( void (*detach)( void ), void (*attach)( void ) );

/* Stops the plugins and unloads the library. Every bar's widgets must have
 * been cleared before. */
void awl_plugins_unload( void );

void awl_plugins_set_paused( int paused );

/* Fills an empty bar's widget lists; no-op without a loaded library. */
void awl_plugins_bar_widgets( Drwl* bar );

/* The loaded library's table, NULL if none. Changes on reload, so don't
 * keep it (or anything from it) across one. */
typedef struct awl_plugin_api_t awl_plugin_api_t;
const awl_plugin_api_t* awl_plugins_api( void );
