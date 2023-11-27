#pragma once

// TODO make this threadsafe (mutex + no static data)

void wallpaper_init( const char* fname, int update_seconds );
void wallpaper_destroy( void );
