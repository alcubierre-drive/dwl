#pragma once

struct awl_ipaddr_t {
    char address_string[2047];
    unsigned char is_online;
};
typedef struct awl_ipaddr_t awl_ipaddr_t;

void start_ip_thread( awl_ipaddr_t* ip, int update_sec );
void stop_ip_thread( void );
