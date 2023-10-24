#pragma once

typedef struct awl_title_t awl_title_t;
struct awl_title_t {
    char name[126];
    unsigned char urgent;
    unsigned char focused;
};
