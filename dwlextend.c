#define DWLEXTEND_COMPILATION
#include "extension_callbacks.h"

static Key internal_keys[1024] = {0};
static int internal_keys_len = -1;
static callbacks_t* internal_callbacks = NULL;

void EX_init( callbacks_t* c ) {
    internal_callbacks = c;
}

void EX_viewCycle( callbacks_t* c, const Arg* arg ) {
    internal_callbacks = c;
    if (!c) return;

    Monitor* selmon = c->get_selmon();
    if (!selmon) return;

    uint32_t c_tag = selmon->tagset[selmon->seltags] & TAGMASK;
    int32_t c_count = 0;
    while (c_tag > 1) { c_tag/=2; c_count++; };

    selmon->seltags ^= 1; /* toggle sel tagset */
    if (arg->ui & TAGMASK)
        selmon->tagset[selmon->seltags] = (1 << ((arg->i + TAGCOUNT + c_count)%TAGCOUNT)) & TAGMASK;
    c->focusclient(c->focustop(selmon), 1);
    c->arrange(selmon);
    c->printstatus();
}

#define ADD_KEY( MOD, KEY, FUN, ARG ) \
    internal_keys[internal_keys_len++] = (Key){ MOD , KEY, \
        internal_callbacks->extension_call, {.ex = {.name= #FUN , .uarg ARG }} };
Key* EX_keys( int* len ) {
    if (!internal_callbacks) {
        *len = 0;
        return NULL;
    }
    if (internal_keys_len == -1) {
        internal_keys_len = 0;
        ADD_KEY( MODKEY, XKB_KEY_Right, EX_viewCycle, .i=1 )
        ADD_KEY( MODKEY, XKB_KEY_Left, EX_viewCycle, .i=-1 )
    }
    *len = internal_keys_len;
    return internal_keys;
}
