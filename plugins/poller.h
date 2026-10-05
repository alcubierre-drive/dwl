#pragma once

#include "stats.h"
#include "temp.h"
#include "date.h"
#include "bat.h"
#include "ipaddr.h"

/* One thread drives all of the plugins above: stats, temperature and clock
 * tick once per wall-clock second, battery and IP address only wake it when
 * the kernel reports a change. Every wakeup ends in at most one redraw
 * request. */
typedef struct awl_poller_t awl_poller_t;

awl_poller_t* poller_start( awl_stats_t* stats, awl_temperature_t* temp, awl_date_t* date,
                            awl_battery_t* bat, awl_ipaddr_t* ip );
/* Paused, the 1 s tick stops (battery and IP events are still handled);
 * resuming re-reads everything and requests a redraw. Any thread. */
void poller_set_paused( awl_poller_t* p, int paused );
/* Returns once the thread has exited; the plugin data can be freed then. */
void poller_stop( awl_poller_t* p );
