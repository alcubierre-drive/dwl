#include "poller.h"
#include "pthread_wrap.h"
#include "redraw.h"
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/prctl.h>

/* Lets the kernel delay the 1 s tick by this much to batch it with other
 * wakeups; nobody sees the clock or the graph being 20 ms late. */
#define TIMER_SLACK_NS 20000000
/* Safety nets, run on the 1 s tick so they never cause a wakeup of their
 * own: some firmware doesn't send a uevent for every capacity step, and
 * without the netlink socket the IP address would never update. */
#define BAT_REREAD_TICKS 60
#define IP_FALLBACK_TICKS 5

struct awl_poller_t {
    awl_stats_t* stats;
    awl_temperature_t* temp;
    awl_date_t* date;
    awl_battery_t* bat;
    awl_ipaddr_t* ip;

    pthread_t me;
    int wake_fd; // eventfd; written by poller_stop() to end the thread
};

/* poll() timeout until the next full wall-clock second, rounded up so it
 * never expires before it */
static int ms_to_next_second( const struct timespec* now ) {
    long ns = 1000000000L - now->tv_nsec;
    return (int)((ns + 999999L) / 1000000L);
}

static void* poller_run( void* arg ) {
    awl_poller_t* p = arg;
    prctl( PR_SET_TIMERSLACK, TIMER_SLACK_NS, 0, 0, 0 );

    stats_update( p->stats );
    temp_update( p->temp );
    date_update( p->date );
    bat_update( p->bat );
    ip_update( p->ip );
    awl_redraw_request();

    struct pollfd fds[3] = {
        { .fd = p->wake_fd, .events = POLLIN },
        { .fd = p->bat->uevent_fd, .events = POLLIN }, // poll() skips fd -1
        { .fd = p->ip->nl_fd, .events = POLLIN },
    };
    struct timespec now;
    clock_gettime( CLOCK_REALTIME, &now );
    time_t last_tick = now.tv_sec;
    unsigned long ticks = 0;

    while (1) {
        clock_gettime( CLOCK_REALTIME, &now );
        if (poll( fds, 3, ms_to_next_second( &now ) ) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (fds[0].revents) break;

        int changed = 0;
        if (fds[1].revents) changed |= bat_dispatch( p->bat );
        if (fds[2].revents) changed |= ip_dispatch( p->ip );

        // tick whenever the wall-clock second changed, however we woke up
        // (also covers clock jumps and resume from suspend)
        clock_gettime( CLOCK_REALTIME, &now );
        if (now.tv_sec != last_tick) {
            last_tick = now.tv_sec;
            ticks++;
            stats_update( p->stats );
            changed = 1;
            changed |= temp_update( p->temp );
            changed |= date_update( p->date );
            if (ticks % BAT_REREAD_TICKS == 0)
                changed |= bat_update( p->bat );
            if (p->ip->nl_fd < 0 && ticks % IP_FALLBACK_TICKS == 0)
                changed |= ip_update( p->ip );
        }

        if (changed) awl_redraw_request();
    }
    return NULL;
}

awl_poller_t* poller_start( awl_stats_t* stats, awl_temperature_t* temp, awl_date_t* date,
                            awl_battery_t* bat, awl_ipaddr_t* ip ) {
    awl_poller_t* p = calloc(1, sizeof(awl_poller_t));
    p->stats = stats;
    p->temp = temp;
    p->date = date;
    p->bat = bat;
    p->ip = ip;
    p->wake_fd = eventfd( 0, EFD_CLOEXEC );
    AWL_PTHREAD_CREATE( &p->me, NULL, poller_run, p );
    return p;
}

void poller_stop( awl_poller_t* p ) {
    uint64_t one = 1;
    if (p->wake_fd >= 0 && write( p->wake_fd, &one, sizeof one ) == sizeof one) {
        pthread_join( p->me, NULL );
        close( p->wake_fd );
    } else if (!pthread_cancel( p->me )) {
        // no eventfd: the thread can only be cancelled
        pthread_join( p->me, NULL );
    }
    free( p );
}
