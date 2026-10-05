#define _GNU_SOURCE
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include "ipaddr.h"

static int is_not_in_exclude_list( const char* name, char exclude_list[4][16], int nexclude );

/* The kernel announces every IPv4 address that is added or removed (DHCP
 * lease, link down, VPN, ...) on this multicast group; nothing else is shown
 * by the widget, so nothing else needs to wake us. */
static int rtnl_open( void ) {
    int fd = socket( AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_ROUTE );
    if (fd < 0) return -1;
    struct sockaddr_nl sa = { .nl_family = AF_NETLINK, .nl_groups = RTMGRP_IPV4_IFADDR };
    if (bind( fd, (struct sockaddr*)&sa, sizeof sa ) < 0) {
        close( fd );
        return -1;
    }
    return fd;
}

awl_ipaddr_t* ip_init( void ) {
    awl_ipaddr_t* ip = calloc(1, sizeof(awl_ipaddr_t));
    strcpy( ip->exclude_list[ip->n_exclude_list++], "lo" );
    strcpy( ip->exclude_list[ip->n_exclude_list++], "virbr0" );
    strcpy( ip->exclude_list[ip->n_exclude_list++], "docker0" );
    for (int i=0; i<AWL_IP_MAX; ++i)
        atomic_init( &ip->addr[i], 0 );
    atomic_init( &ip->n_addr, 0 );
    ip->nl_fd = rtnl_open();
    return ip;
}

void ip_free( awl_ipaddr_t* ip ) {
    if (ip->nl_fd >= 0) close( ip->nl_fd );
    free(ip);
}

int ip_dispatch( awl_ipaddr_t* ip ) {
    // the messages only tell us that something changed; getifaddrs() has
    // the full picture. ENOBUFS (lost events) ends up here as well.
    char buf[8192];
    while (recv( ip->nl_fd, buf, sizeof buf, 0 ) > 0) {}
    return ip_update( ip );
}

int ip_update( awl_ipaddr_t* ip ) {
    uint32_t addr[AWL_IP_MAX] = {0};
    int n = 0;

    struct ifaddrs *ifaddr;
    if (getifaddrs(&ifaddr) == -1) {
        /*P_awl_err_printf("getifaddrs failed");*/
        return 0;
    }

    for (struct ifaddrs *ifa = ifaddr; ifa != NULL && n < AWL_IP_MAX; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        int family = ifa->ifa_addr->sa_family;
        if (family == AF_INET && is_not_in_exclude_list(ifa->ifa_name, ip->exclude_list, ip->n_exclude_list))
            addr[n++] = ((struct sockaddr_in*)ifa->ifa_addr)->sin_addr.s_addr;
    }
    freeifaddrs(ifaddr);

    // addresses first, so a reader seeing the new count sees them too
    int changed = 0;
    for (int i=0; i<AWL_IP_MAX; ++i)
        changed |= atomic_exchange( &ip->addr[i], addr[i] ) != addr[i];
    changed |= atomic_exchange( &ip->n_addr, n ) != n;
    return changed;
}

static int is_not_in_exclude_list( const char* name, char exclude_list[4][16], int nexclude ) {
    int is_in_list = 0;
    for (int i=0; i<nexclude; ++i)
        is_in_list += !strcmp(name, exclude_list[i]);
    return !is_in_list;
}
