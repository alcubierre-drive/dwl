#pragma once

#include "awl.h"

typedef struct LayerSurface LayerSurface;

void arrange(Monitor *m);
void focusclient(Client *c, int lift);
Client* focustop(Monitor *m);
void printstatus(void);
void resize(Client *c, struct wlr_box geo, int interact);
Monitor* dirtomon(enum wlr_direction dir);
void setfloating(Client *c, int floating);
void setontop(Client *c, int ontop);
void xytonode(double x, double y, struct wlr_surface **psurface, Client **pc,
        LayerSurface **pl, double *nx, double *ny);
void setmon(Client *c, Monitor *m, uint32_t newtags);
void setfullscreen(Client *c, int fullscreen);
void ipc_send_toggle_vis( struct wl_resource* resource );

void awl_change_modkey( uint32_t modkey );
int awl_is_ready( void );

