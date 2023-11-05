#pragma once

#include <uthash.h>

typedef struct Client Client;
typedef struct awl_title_t awl_title_t;

struct awl_title_t {
    char name[128];
    unsigned char floating;
    unsigned char urgent;
    unsigned char focused;
    unsigned char visible;

    // make this hashable
    UT_hash_handle hh;
    union {
        uint64_t id;
        Client* c;
    };
};
