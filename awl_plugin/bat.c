#include "bat.h"
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

static pthread_t bat_thread;
static int bat_run = 0;
static int bat_update_sec = -1;

static const char bat_prefix[] = "/sys/class/power_supply/BAT0/";

static void* bat( void* arg ) {
    if (!arg) return NULL;
    awl_battery_t* b = arg;
    while (bat_run) {
        char bat_file[128];

        FILE* f = NULL;
        unsigned long u = 0;
        int set = -3;

        float charge = 0.0;
        int charging = 0;
        strcpy( bat_file, bat_prefix );
        strcat( bat_file, "charge_now" );
        if ((f = fopen( bat_file, "r" ))) {
            if (fscanf(f, "%lu", &u)) charge = u;
            fclose(f);
            set++;
        }
        strcpy( bat_file, bat_prefix );
        strcat( bat_file, "charge_full" );
        if ((f = fopen( bat_file, "r" ))) {
            if (fscanf(f, "%lu", &u)) charge /= (u > 0) ? (float)u : 1.0;
            fclose(f);
            set++;
        }
        strcpy( bat_file, bat_prefix );
        strcat( bat_file, "status" );
        if ((f = fopen( bat_file, "r" ))) {
            memset( bat_file, 0, sizeof(bat_file) );
            fread( bat_file, 1, sizeof(bat_file)-1, f );
            fclose(f);
            if (strstr(bat_file, "Charging")) charging = 1;
            set++;
        }

        if (set) charging = -1;
        b->charging = charging;
        b->charge = charge;
        sleep( bat_update_sec );
    }
    return NULL;
}

void start_bat_thread( awl_battery_t* b, int update_sec ) {
    bat_run = 1;
    bat_update_sec = update_sec;
    pthread_create( &bat_thread, NULL, bat, b );
}

void stop_bat_thread( void ) {
    pthread_cancel( bat_thread );
    pthread_join( bat_thread, NULL );
}
