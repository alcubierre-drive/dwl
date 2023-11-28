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
#include "../awl_pthread.h"

static int is_not_in_exclude_list( const char* name, char exclude_list[4][16], int nexclude );

static void* ip_thread_run( void* arg ) {
    awl_ipaddr_t* ip = (awl_ipaddr_t*)arg;
    if (!ip) return NULL;
    while (ip->running) {

        pthread_mutex_lock( &ip->mtx );
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
            if (family == AF_INET && is_not_in_exclude_list(ifa->ifa_name, ip->exclude_list, ip->n_exclude_list)) {
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
        freeifaddrs(ifaddr);
        ip->ready = 1;

        pthread_mutex_unlock( &ip->mtx );
        sleep(ip->sleep_sec);
    }

    return NULL;
}

awl_ipaddr_t* start_ip_thread( int update_sec ) {
    awl_ipaddr_t* ip = calloc(1, sizeof(awl_ipaddr_t));
    ip->running = 1;
    ip->sleep_sec = update_sec;
    strcpy( ip->exclude_list[ip->n_exclude_list++], "lo" );
    strcpy( ip->exclude_list[ip->n_exclude_list++], "virbr0" );

    P_awl_log_printf( "create ip_thread" );
    pthread_mutex_init( &ip->mtx, NULL );
    AWL_PTHREAD_CREATE( &ip->me, NULL, ip_thread_run, ip );
    return ip;
}

void stop_ip_thread( awl_ipaddr_t* ip ) {
    pthread_mutex_lock( &ip->mtx );
    if (!pthread_cancel( ip->me )) pthread_join( ip->me, NULL );
    pthread_mutex_unlock( &ip->mtx );
    pthread_mutex_destroy( &ip->mtx );
    free(ip);
    P_awl_log_printf("cancelled ip thread");
}

static int is_not_in_exclude_list( const char* name, char exclude_list[4][16], int nexclude ) {
    int is_in_list = 0;
    for (int i=0; i<nexclude; ++i)
        is_in_list += !strcmp(name, exclude_list[i]);
    return !is_in_list;
}

