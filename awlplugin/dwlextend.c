#define DWLEXTEND_COMPILATION
#include "extension_callbacks.h"

static Key internal_keys[1024] = {0};
static int internal_keys_len = -1;
static callbacks_t* internal_callbacks = NULL;

static Layout internal_layouts[1024] = {0};
static int internal_layouts_len = 0;
static int internal_layouts_cur = 0;

void EX_init( callbacks_t* c ) {
    internal_callbacks = c;
    if (c) {
        internal_layouts[internal_layouts_len++] = (Layout){ "[]=", c->tile };
        internal_layouts[internal_layouts_len++] = (Layout){ "><>", NULL };
        internal_layouts[internal_layouts_len++] = (Layout){ "[M]", c->monocle };
        internal_layouts_cur = 0;
    }
}

void EX_finalize( void ) {
    memset( internal_keys, 0, sizeof(internal_keys) );
    internal_keys_len = -1;
    internal_callbacks = NULL;
    memset( internal_layouts, 0, sizeof(internal_layouts) );
    internal_layouts_len = 0;
    internal_layouts_cur = 0;
}

void EX_viewCycle( const Arg* arg ) {
    callbacks_t* c = internal_callbacks;
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

void EX_layoutCycle( const Arg* arg ) {
    (void)arg;
    callbacks_t* c = internal_callbacks;
    if (!c) return;

    internal_layouts_cur = (internal_layouts_cur + 1)%internal_layouts_len;
    Arg A = {.v = &internal_layouts[internal_layouts_cur]};
    c->setlayout( &A );
    c->focusclient(c->focustop(c->get_selmon()), 1);
}

static const char *termcmd[] = {"kitty", "--single-instance", NULL};
static const char *menucmd[] = {"bemenu-run", NULL};

#define MODKEY_SH MODKEY|WLR_MODIFIER_SHIFT
#define MODKEY_CT MODKEY|WLR_MODIFIER_CTRL
#define MODKEY_CT_SH MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_SHIFT
#define ADD_KEY( MOD, KEY, FUN, ARG ) \
    internal_keys[internal_keys_len++] = (Key){ MOD, KEY, FUN, ARG };
#define ADD_INT( MOD, KEY, FUN, ARG ) \
    internal_keys[internal_keys_len++] = (Key){ MOD, KEY, \
        internal_callbacks->FUN, ARG };
#define ADD_TAG( KEY, SKEY, TAG) \
    ADD_INT( MODKEY,       KEY,  view,       {.ui = 1 << TAG} ) \
    ADD_INT( MODKEY_CT,    KEY,  toggleview, {.ui = 1 << TAG} ) \
    ADD_INT( MODKEY_SH,    SKEY, tag,        {.ui = 1 << TAG} ) \
    ADD_INT( MODKEY_CT_SH, SKEY, toggletag,  {.ui = 1 << TAG} )

Key* EX_keys( int* len ) {
    if (!internal_callbacks) {
        *len = 0;
        return NULL;
    }
    if (internal_keys_len == -1) {
        internal_keys_len = 0;
        ADD_KEY( MODKEY, XKB_KEY_Right, EX_viewCycle,   {.i= 1} )
        ADD_KEY( MODKEY, XKB_KEY_Left,  EX_viewCycle,   {.i=-1} )
        ADD_KEY( MODKEY, XKB_KEY_space, EX_layoutCycle, {0} )

    }
    *len = internal_keys_len;
    return internal_keys;
}

Layout* EX_layouts( int* len ) {
    if (!internal_callbacks) {
        *len = 0;
        return NULL;
    }
    *len = internal_layouts_len;
    return internal_layouts;
}
