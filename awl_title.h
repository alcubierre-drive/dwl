#pragma once

typedef struct awl_title_t awl_title_t;
struct awl_title_t {
    char name[125];
    unsigned char urgent;
    unsigned char focused;
    unsigned char visible;
};
