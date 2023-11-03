#pragma once

typedef struct awl_title_t awl_title_t;
struct awl_title_t {
    char name[124];
    unsigned char floating;
    unsigned char urgent;
    unsigned char focused;
    unsigned char visible;
};
