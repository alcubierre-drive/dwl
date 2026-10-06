#pragma once

#include <stdatomic.h>
#include <stdint.h>

/** The most addresses `awl_ipaddr_t` keeps. */
#define AWL_IP_MAX 8

/** The machine's IPv4 addresses. */
struct awl_ipaddr_t {
    /** output: the first n_addr IPv4 addresses (network byte order) of the
     * interfaces not excluded below; none means disconnected */
    _Atomic uint32_t addr[AWL_IP_MAX];
    atomic_int n_addr;

    /** input, read-only after `ip_init()`: interface names to skip */
    char exclude_list[4][16];
    int n_exclude_list;

    /** rtnetlink IPv4 address events, -1 if unavailable */
    int nl_fd;
};
typedef struct awl_ipaddr_t awl_ipaddr_t;

awl_ipaddr_t* ip_init( void );
/** Re-reads the addresses; returns nonzero if they changed. */
int ip_update( awl_ipaddr_t* ip );
/** Call when ``nl_fd`` is readable. Drains it and re-reads; returns like
 * `ip_update()`. */
int ip_dispatch( awl_ipaddr_t* ip );
void ip_free( awl_ipaddr_t* ip );
