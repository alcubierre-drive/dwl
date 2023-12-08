#pragma once

#include "../awl_pthread.h"
#include <semaphore.h>

struct awl_ipaddr_t {
    char address[128];
    _Atomic int is_online;

    char exclude_list[4][16];
    int n_exclude_list;
    int sleep_sec, running;

    pthread_t me;
    sem_t sem;
};
typedef struct awl_ipaddr_t awl_ipaddr_t;

awl_ipaddr_t* start_ip_thread( int update_sec );
void stop_ip_thread( awl_ipaddr_t* ip );
