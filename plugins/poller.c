#include "poller.h"
#include "pthread_wrap.h"
#include "redraw.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
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
    int wake_fd; // eventfd; written by poller_stop() and poller_set_paused()
    atomic_int stop, paused;
};

/* poll() timeout until the next full wall-clock second, rounded up so it
 * never expires before it */
static int ms_to_next_second( const struct timespec* now ) {
    long ns = 1000000000L - now->tv_nsec;
    return (int)((ns + 999999L) / 1000000L);
}

static void update_all( awl_poller_t* p ) {
    stats_update( p->stats );
    temp_update( p->temp );
    date_update( p->date );
    bat_update( p->bat );
    ip_update( p->ip );
    awl_redraw_request();
}

static void* poller_run( void* arg ) {
    awl_poller_t* p = arg;
    prctl( PR_SET_TIMERSLACK, TIMER_SLACK_NS, 0, 0, 0 );

    update_all( p );
    int was_paused = 0;

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
        // paused: no tick, only the battery and IP events (rare) keep their
        // state current
        int paused = atomic_load( &p->paused );
        if (was_paused && !paused) {
            // the bar is visible again: catch up on everything at once
            update_all( p );
            clock_gettime( CLOCK_REALTIME, &now );
            last_tick = now.tv_sec;
        }
        was_paused = paused;

        clock_gettime( CLOCK_REALTIME, &now );
        if (poll( fds, 3, paused ? -1 : ms_to_next_second( &now ) ) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (fds[0].revents) {
            uint64_t n;
            if (read( p->wake_fd, &n, sizeof n ) < 0 && errno != EAGAIN) break;
            if (atomic_load( &p->stop )) break;
        }

        int changed = 0;
        if (fds[1].revents) changed |= bat_dispatch( p->bat );
        if (fds[2].revents) changed |= ip_dispatch( p->ip );

        // tick whenever the wall-clock second changed, however we woke up
        // (also covers clock jumps and resume from suspend)
        clock_gettime( CLOCK_REALTIME, &now );
        if (!paused && now.tv_sec != last_tick) {
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

        if (changed && !paused) awl_redraw_request();
    }
    return NULL;
}

awl_poller_t* poller_start( awl_stats_t* stats, awl_temperature_t* temp, awl_date_t* date,
                            awl_battery_t* bat, awl_ipaddr_t* ip ) {
    awl_poller_t* p = calloc(1, sizeof(awl_poller_t));
    if (!p) return NULL;
    p->stats = stats;
    p->temp = temp;
    p->date = date;
    p->bat = bat;
    p->ip = ip;
    atomic_init( &p->stop, 0 );
    atomic_init( &p->paused, 0 );
    // the eventfd is the only way to stop the thread
    p->wake_fd = eventfd( 0, EFD_CLOEXEC | EFD_NONBLOCK );
    if (p->wake_fd < 0) {
        perror( "poller: eventfd" );
        free( p );
        return NULL;
    }
    int err = AWL_PTHREAD_CREATE( &p->me, NULL, poller_run, p );
    if (err) {
        fprintf( stderr, "poller: can't start the thread: %s\n", strerror( err ) );
        close( p->wake_fd );
        free( p );
        return NULL;
    }
    return p;
}

void poller_set_paused( awl_poller_t* p, int paused ) {
    if (!p) return;
    if (atomic_exchange( &p->paused, !!paused ) == !!paused) return;
    uint64_t one = 1;
    // only fails if the counter is full, i.e. the thread is due to wake anyway
    if (write( p->wake_fd, &one, sizeof one ) < 0) {}
}

void poller_stop( awl_poller_t* p ) {
    if (!p) return;
    uint64_t one = 1;
    atomic_store( &p->stop, 1 );
    // only fails if the counter is full, i.e. the thread is due to wake anyway
    if (write( p->wake_fd, &one, sizeof one ) < 0) {}
    pthread_join( p->me, NULL );
    close( p->wake_fd );
    free( p );
}
