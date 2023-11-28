#include "bat.h"
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "init.h"
#include "../awl_log.h"
#include "../awl_pthread.h"

static const char bat_prefix[] = "/sys/class/power_supply/BAT0/";

static void* bat( void* arg ) {
    if (!arg) return NULL;
    awl_battery_t* b = arg;

    while (1) {
        pthread_mutex_lock( &b->mtx );
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
        pthread_mutex_unlock( &b->mtx );
        sleep( b->update_sec );
    }
    return NULL;
}

awl_battery_t* start_bat_thread( int update_sec ) {
    awl_battery_t* b = calloc(1, sizeof(awl_battery_t));
    b->update_sec = update_sec;
    pthread_mutex_init( &b->mtx, NULL );
    P_awl_log_printf( "creating bat_thread" );
    AWL_PTHREAD_CREATE( &b->me, NULL, bat, b );
    return b;
}

void stop_bat_thread( awl_battery_t* b ) {
    pthread_mutex_lock(&b->mtx);
    if (!pthread_cancel(b->me)) pthread_join( b->me, NULL );
    pthread_mutex_unlock(&b->mtx);
    pthread_mutex_destroy(&b->mtx);
    free(b);
}
