#define _GNU_SOURCE
#include <arpa/inet.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdatomic.h>
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
    ip->nl_fd = rtnl_open();
    sem_init( &ip->sem, 0, 1 );
    return ip;
}

void ip_free( awl_ipaddr_t* ip ) {
    if (ip->nl_fd >= 0) close( ip->nl_fd );
    sem_destroy( &ip->sem );
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
    int is_online = 1;
    char new_addr[128] = {0};
    size_t len = 0;

    struct ifaddrs *ifaddr;
    if (getifaddrs(&ifaddr) == -1) {
        /*P_awl_err_printf("getifaddrs failed");*/
        return 0;
    }

    for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        int family = ifa->ifa_addr->sa_family;
        if (family == AF_INET && is_not_in_exclude_list(ifa->ifa_name, ip->exclude_list, ip->n_exclude_list)) {
            char host[INET_ADDRSTRLEN];
            if (!inet_ntop( AF_INET, &((struct sockaddr_in*)ifa->ifa_addr)->sin_addr, host, sizeof host ))
                continue;
            if (strstr(host, "127.0.0"))
                is_online = 0;
            // truncates instead of overflowing with many interfaces
            int w = snprintf( new_addr+len, sizeof(new_addr)-len, "%s%s", len ? " | " : "", host );
            if (w > 0) len += (size_t)w;
            if (len >= sizeof(new_addr)) len = sizeof(new_addr)-1;
        }
    }
    freeifaddrs(ifaddr);

    if (!*new_addr) {
        is_online = 0;
        strcpy(new_addr, "invalid");
    }

    sem_wait( &ip->sem );
    int changed = strcmp( ip->address, new_addr ) != 0;
    if (changed) memcpy( ip->address, new_addr, sizeof new_addr );
    sem_post( &ip->sem );
    changed |= atomic_exchange( &ip->is_online, is_online ) != is_online;
    return changed;
}

static int is_not_in_exclude_list( const char* name, char exclude_list[4][16], int nexclude ) {
    int is_in_list = 0;
    for (int i=0; i<nexclude; ++i)
        is_in_list += !strcmp(name, exclude_list[i]);
    return !is_in_list;
}
