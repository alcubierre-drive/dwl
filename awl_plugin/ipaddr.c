#define _GNU_SOURCE
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/if_link.h>
#include <string.h>
#include <pthread.h>

#include "ipaddr.h"
#include "init.h"
#include "../awl_log.h"

static int ip_thread_running = 0;
static int ip_thread_sleep_sec = -1;

static const char exclude_list[][16] = {
    "lo",
    "virbr0",
};

static int is_not_in_exclude_list( const char* name ) {
    int nexclude = sizeof(exclude_list) / sizeof(exclude_list[0]);
    int is_in_list = 0;
    for (int i=0; i<nexclude; ++i)
        is_in_list += !strcmp(name, exclude_list[i]);
    return !is_in_list;
}

static void* ip_thread_run( void* arg ) {
    awl_ipaddr_t* ip = (awl_ipaddr_t*)arg;
    if (!ip) return NULL;
    while (ip_thread_running) {

        ip->is_online = 1;
        ip->ready = 0;

        int first = 1;
        char* addr = ip->address_string;
        memset(addr, 0, sizeof(ip->address_string));

        struct ifaddrs *ifaddr;

        if (getifaddrs(&ifaddr) == -1) {
            P_awl_err_printf("getifaddrs failed");
            continue;
        }

        for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL) continue;
            int family = ifa->ifa_addr->sa_family;
            if (family == AF_INET && is_not_in_exclude_list(ifa->ifa_name)) {
                char host[NI_MAXHOST];
                int s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host,
                        NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
                if (s) {
                    P_awl_err_printf("getnameinfo() failed: %s\n", gai_strerror(s));
                    goto loopend;
                }
                if (first) {
                    strcat(addr, host);
                    first = 0;
                } else {
                    strcat(addr, " | ");
                    strcat(addr, host );
                }
                if (strstr(addr, "127.0.0"))
                    ip->is_online = 0;
                addr += strlen(addr);
            }
        }
        if (!*ip->address_string) {
            ip->is_online = 0;
            strcat(ip->address_string, "invalid");
        }
loopend:
        ip->ready = 1;
        freeifaddrs(ifaddr);
        sleep(ip_thread_sleep_sec);
    }

    return NULL;
}

static pthread_t ip_thread;
void start_ip_thread( awl_ipaddr_t* ip, int update_sec ) {
    ip_thread_running = 1;
    ip_thread_sleep_sec = update_sec;
    pthread_create( &ip_thread, NULL, ip_thread_run, (void*)ip );
}
void stop_ip_thread( void ) {
    ip_thread_sleep_sec = ip_thread_running = 0;
    pthread_cancel( ip_thread );
    pthread_join( ip_thread, NULL );
}
