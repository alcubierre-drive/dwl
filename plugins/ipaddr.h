#pragma once

#include <semaphore.h>

struct awl_ipaddr_t {
    char address[128];
    _Atomic int is_online;

    char exclude_list[4][16];
    int n_exclude_list;

    int nl_fd; // rtnetlink IPv4 address events, -1 if unavailable

    sem_t sem;
};
typedef struct awl_ipaddr_t awl_ipaddr_t;

awl_ipaddr_t* ip_init( void );
/* Re-reads the addresses; returns nonzero if address or online state
 * changed. */
int ip_update( awl_ipaddr_t* ip );
/* Call when nl_fd is readable. Drains it and re-reads; returns like
 * ip_update(). */
int ip_dispatch( awl_ipaddr_t* ip );
void ip_free( awl_ipaddr_t* ip );
