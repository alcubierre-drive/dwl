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
    while (ip->running) {
        sem_post( &ip->sem );
        sleep(ip->sleep_sec);
        sem_wait( &ip->sem );

        int is_online = 1;
        int first = 1;
        int do_freeifaddrs = 0;
        char* new_addr = calloc(1, 2048);
        char* addr = new_addr;

        struct ifaddrs *ifaddr;

        if (getifaddrs(&ifaddr) == -1) {
            P_awl_err_printf("getifaddrs failed");
            goto loopend;
        }

        for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL)
                goto loopend;
            int family = ifa->ifa_addr->sa_family;
            if (family == AF_INET && is_not_in_exclude_list(ifa->ifa_name, ip->exclude_list, ip->n_exclude_list)) {
                char host[NI_MAXHOST];
                int s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host,
                        NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
                if (s) {
                    P_awl_err_printf("getnameinfo() failed: %s\n", gai_strerror(s));
                    do_freeifaddrs = 1;
                    goto loopend;
                }

                if (first) first = 0;
                else strcat(addr, " | ");
                strcat(addr, host);

                if (strstr(addr, "127.0.0"))
                    is_online = 0;
                addr += strlen(addr);
            }
        }
        if (!*new_addr) {
            is_online = 0;
            strcpy(new_addr, "invalid");
        }

loopend:
        if (do_freeifaddrs) freeifaddrs(ifaddr);

        char* old_addr = (char*)atomic_load( &ip->address );
        atomic_store( &ip->address, (_Atomic char*)new_addr );
        atomic_store( &ip->is_online, is_online );
        free( old_addr );
    }

    return NULL;
}

awl_ipaddr_t* start_ip_thread( int update_sec ) {
    awl_ipaddr_t* ip = calloc(1, sizeof(awl_ipaddr_t));
    ip->running = 1;
    ip->sleep_sec = update_sec;
    strcpy( ip->exclude_list[ip->n_exclude_list++], "lo" );
    strcpy( ip->exclude_list[ip->n_exclude_list++], "virbr0" );
    ip->address = calloc(1, 2048);
    sem_init( &ip->sem, 0, 1 );

    P_awl_log_printf( "create ip_thread" );
    AWL_PTHREAD_CREATE( &ip->me, NULL, ip_thread_run, ip );
    return ip;
}

void stop_ip_thread( awl_ipaddr_t* ip ) {
    sem_wait( &ip->sem );
    if (!pthread_cancel( ip->me )) pthread_join( ip->me, NULL );
    sem_destroy( &ip->sem );
    free(ip->address);
    free(ip);
    P_awl_log_printf("cancelled ip thread");
}

static int is_not_in_exclude_list( const char* name, char exclude_list[4][16], int nexclude ) {
    int is_in_list = 0;
    for (int i=0; i<nexclude; ++i)
        is_in_list += !strcmp(name, exclude_list[i]);
    return !is_in_list;
}

