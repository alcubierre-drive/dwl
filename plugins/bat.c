#include "bat.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <linux/netlink.h>

static const char bat_prefix[] = "/sys/class/power_supply/BAT0/";

/* The kernel sends a uevent whenever a power_supply device changes: the AC
 * adapter on plug/unplug, the battery on status changes and (on most
 * firmware) on every capacity step. These are the same events udev reads. */
static int uevent_open( void ) {
    int fd = socket( AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_KOBJECT_UEVENT );
    if (fd < 0) return -1;
    struct sockaddr_nl sa = { .nl_family = AF_NETLINK, .nl_groups = 1 /* kernel */ };
    if (bind( fd, (struct sockaddr*)&sa, sizeof sa ) < 0) {
        close( fd );
        return -1;
    }
    return fd;
}

awl_battery_t* bat_init( void ) {
    awl_battery_t* b = calloc(1, sizeof(awl_battery_t));
    atomic_init( &b->charge, 0 );
    atomic_init( &b->charging, -1 );
    b->uevent_fd = uevent_open();
    return b;
}

void bat_free( awl_battery_t* b ) {
    if (b->uevent_fd >= 0) close( b->uevent_fd );
    free(b);
}

int bat_dispatch( awl_battery_t* b ) {
    int relevant = 0;
    char buf[8192];
    ssize_t n;
    // "action@devpath\0KEY=VALUE\0KEY=VALUE\0..."
    while ((n = recv( b->uevent_fd, buf, sizeof(buf)-1, 0 )) > 0) {
        buf[n] = '\0';
        for (char* kv = buf; kv < buf+n; kv += strlen(kv)+1)
            if (!strcmp( kv, "SUBSYSTEM=power_supply" )) relevant = 1;
    }
    // ENOBUFS: we lost events, so re-read to be safe
    return relevant || (n < 0 && errno == ENOBUFS) ? bat_update( b ) : 0;
}

int bat_update( awl_battery_t* b ) {
    char bat_file[128];

    FILE* f = NULL;
    unsigned long u = 0;
    int set = -3;

    float charge = 0.0;
    int charging = 0;
    strcpy( bat_file, bat_prefix );
    strcat( bat_file, "energy_now" );
    if ((f = fopen( bat_file, "r" ))) {
        if (fscanf(f, "%lu", &u) == 1) charge = u;
        fclose(f);
        set++;
    }
    strcpy( bat_file, bat_prefix );
    strcat( bat_file, "energy_full" );
    if ((f = fopen( bat_file, "r" ))) {
        if (fscanf(f, "%lu", &u) == 1) charge /= (u > 0) ? (float)u : 1.0;
        fclose(f);
        set++;
    }
    strcpy( bat_file, bat_prefix );
    strcat( bat_file, "status" );
    if ((f = fopen( bat_file, "r" ))) {
        memset( bat_file, 0, sizeof(bat_file) );
        if (fread( bat_file, 1, sizeof(bat_file)-1, f )) {}
        fclose(f);
        if (strstr(bat_file, "Charging")) charging = 1;
        set++;
    }

    if (set) charging = -1;
    int changed = atomic_exchange( &b->charging, charging ) != charging;
    changed |= atomic_exchange( &b->charge, charge ) != charge;
    return changed;
}
