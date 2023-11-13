#pragma once

#include <stdint.h>
#include <uthash.h>

typedef struct Client Client;
typedef struct awl_title_t awl_title_t;

struct awl_title_t {
    char name[255];
    struct { uint8_t
        floating:1,
        urgent:1,
        focused:1,
        visible:1,
        maximized:1,
        fullscreen:1;
    };

    // make this hashable
    UT_hash_handle hh;
    union {
        uint64_t id;
        Client* c;
    };
};
