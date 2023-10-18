#pragma once

#include "dwl.h"

typedef void (*focusclient_t)(Client *c, int lift);
typedef Client* (*focustop_t)(Monitor *m);
typedef void (*printstatus_t)(void);
typedef Client* (*get_client_t)(void);
typedef Monitor* (*get_selmon_t)(void);
typedef void (*arrange_t)(Monitor *m);
typedef void (*extension_call_t)(const Arg* arg);

typedef struct {
    focusclient_t focusclient;
    focustop_t focustop;
    printstatus_t printstatus;
    get_client_t get_client;
    get_selmon_t get_selmon;

    arrange_t arrange;
    arrange_t monocle;
    arrange_t tile;

    extension_call_t extension_call;
    extension_call_t extension_reload;

    extension_call_t spawn;
    extension_call_t focusstack;
    extension_call_t incnmaster;
    extension_call_t setmfact;
    extension_call_t zoom;
    extension_call_t toggleview;
    extension_call_t tag;
    extension_call_t toggletag;
    extension_call_t killclient;
    extension_call_t setlayout;
    extension_call_t togglefloating;
    extension_call_t togglefullscreen;
    extension_call_t view;
    extension_call_t focusmon;
    extension_call_t tagmon;
    extension_call_t quit;
} callbacks_t;

