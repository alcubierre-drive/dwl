#pragma once

typedef struct awl_wallpaper_data_t awl_wallpaper_data_t;

awl_wallpaper_data_t* wallpaper_init( const char* fname, int update_seconds );
void wallpaper_destroy( awl_wallpaper_data_t* d );
