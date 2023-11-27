#pragma once

#include <pthread.h>

struct awl_ipaddr_t {
    char address_string[2046];
    unsigned char is_online;
    unsigned char ready;

    pthread_t me;
    int sleep_sec, running;
    char exclude_list[4][16];
    int n_exclude_list;
    pthread_mutex_t mtx;
};
typedef struct awl_ipaddr_t awl_ipaddr_t;

awl_ipaddr_t* start_ip_thread( int update_sec );
void stop_ip_thread( awl_ipaddr_t* ip );
