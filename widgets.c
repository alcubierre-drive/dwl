/* The bar widgets. Built into libawlplugins.so together with the plugins
 * whose data they show, so a reload swaps both at once; everything they need
 * from dwl goes through the host table (awl_plugin_abi.h). */
#include "awl_plugin_abi.h"
#include "plugins.h"
#include "plugins/date.h"
#include "plugins/colors.h"
#include "tray/awl_tray.h"

#include <stdatomic.h>

/* dwl.h's TEXTW calls into dwl directly */
#undef TEXTW
#define TEXTW(mon, text) (awl_host->font_getwidth((mon)->drw, text) + (mon)->lrpad)

static uint32_t tagwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix );
static void tagwidget_scroll( widget_t* w, uint32_t x, int amount );
static void tagwidget_click( widget_t* w, uint32_t x, int button );

static uint32_t layoutwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix );
static void layoutwidget_scroll( widget_t* w, uint32_t x, int amount );
static void layoutwidget_click( widget_t* w, uint32_t x, int button );

static uint32_t taskbarwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix );
static void taskbarwidget_scroll( widget_t* w, uint32_t x, int amount );
static void taskbarwidget_click( widget_t* w, uint32_t x, int button );

/* draw() of the right-hand widgets that show a single text (see textsnap_t) */
static uint32_t textsnap_draw( widget_t* w, uint32_t x, pixman_image_t* pix );

static uint32_t clockwidget_measure( widget_t* w );
static uint32_t clockwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix );
static void clockwidget_click( widget_t* w, uint32_t x, int button );
static void clockwidget_hover( widget_t* w );
static void clockwidget_leave( widget_t* w );

static uint32_t pulsewidget_measure( widget_t* w );
static void pulsewidget_click( widget_t* w, uint32_t x, int button );
static void pulsewidget_scroll( widget_t* w, uint32_t x, int amount );

static uint32_t systray_measure( widget_t* w );
static uint32_t systray_draw( widget_t* w, uint32_t x, pixman_image_t* pix );
static uint32_t statuswidget_measure( widget_t* w );
static uint32_t statuswidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix );
static uint32_t tempwidget_measure( widget_t* w );
static uint32_t tempwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix );
static uint32_t batwidget_measure( widget_t* w );
static uint32_t ipwidget_measure( widget_t* w );

static uint32_t backlightwidget_measure( widget_t* w );
static void backlightwidget_click( widget_t* w, uint32_t x, int button );

void awl_widgets_create( Drwl* drwl ) {
    drwl->widgets_left[drwl->n_widgets_left++] = (widget_t){
        .bar = drwl,
        .draw = &tagwidget_draw,
        .callback_click = &tagwidget_click,
        .callback_scroll = &tagwidget_scroll,
    };
    drwl->widgets_left[drwl->n_widgets_left++] = (widget_t){
        .bar = drwl,
        .draw = &layoutwidget_draw,
        .callback_click = &layoutwidget_click,
        .callback_scroll = &layoutwidget_scroll,
    };

    drwl->center_widget = (widget_t){
        .bar = drwl,
        .draw = &taskbarwidget_draw,
        .callback_click = &taskbarwidget_click,
        .callback_scroll = &taskbarwidget_scroll,
    };
    drwl->has_center_widget = 1;

    drwl->widgets_right[drwl->n_widgets_right++] = (widget_t){
        .bar = drwl,
        .measure = &clockwidget_measure,
        .draw = &clockwidget_draw,
        .callback_click = &clockwidget_click,
        .callback_hover = &clockwidget_hover,
        .hover_delay_ms = 500,
        .callback_leave = &clockwidget_leave,
        .leave_delay_ms = 20,
        .popup_namespace = "awl-calendar:",
        .free = free,
    };
    drwl->widgets_right[drwl->n_widgets_right++] = (widget_t){
        .bar = drwl,
        .measure = &systray_measure,
        .draw = &systray_draw,
    };
    drwl->widgets_right[drwl->n_widgets_right++] = (widget_t){
        .bar = drwl,
        .measure = &pulsewidget_measure,
        .draw = &textsnap_draw,
        .callback_click = &pulsewidget_click,
        .callback_scroll = &pulsewidget_scroll,
        .free = free,
    };
    drwl->widgets_right[drwl->n_widgets_right++] = (widget_t){
        .bar = drwl,
        .measure = &statuswidget_measure,
        .draw = &statuswidget_draw,
        .free = free,
    };
    drwl->widgets_right[drwl->n_widgets_right++] = (widget_t){
        .bar = drwl,
        .measure = &tempwidget_measure,
        .draw = &tempwidget_draw,
        .free = free,
    };
    drwl->widgets_right[drwl->n_widgets_right++] = (widget_t){
        .bar = drwl,
        .measure = &batwidget_measure,
        .draw = &textsnap_draw,
        .free = free,
    };
    drwl->widgets_right[drwl->n_widgets_right++] = (widget_t){
        .bar = drwl,
        .measure = &ipwidget_measure,
        .draw = &textsnap_draw,
        .free = free,
    };
    drwl->widgets_right[drwl->n_widgets_right++] = (widget_t){
        .bar = drwl,
        .measure = &backlightwidget_measure,
        .draw = &textsnap_draw,
        .callback_click = &backlightwidget_click,
        .free = free,
    };
}

#define TEXT( width, string, fg, bg ) \
    awl_host->text( w->bar, x, -2, width, w->bar->m->b.height, w->bar->m->lrpad/2, string, \
            fg, bg )

static uint32_t tagwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix ) {
    awl_plugin_data_t* P = awl_plugin_get();
    if (!P) return 0;
    int width = 0;
    for (int t=0; t<w->bar->ntags; ++t) {
        char num[16] = ""; snprintf( num, sizeof(num)-1, "%i", t+1 );
        int ww = TEXTW(w->bar->m, "0");
        pixman_color_t bg_color = color_8bit_to_16bit(molokai_dark_gray);
        pixman_color_t bg_add;
        if (w->bar->occ & (1 << t)) {
            bg_add = color_8bit_to_16bit(molokai_orange);
            bg_add.alpha = 0xaaaa;
            bg_color = alpha_blend_16(bg_color, bg_add);
        }
        if (w->bar->urg & (1 << t)) {
            bg_add = color_8bit_to_16bit(molokai_red);
            bg_add.alpha = 0xaaaa;
            bg_color = alpha_blend_16(bg_color, bg_add);
        }
        if (w->bar->sel & (1 << t)) {
            bg_add = color_8bit_to_16bit(molokai_purple);
            bg_add.alpha = 0xaaaa;
            bg_color = alpha_blend_16(bg_color, bg_add);
        }
        TEXT( ww, num, P->awl_colors.fg_lay, bg_color );
        x += ww;
        width += ww;
    }
    return width;
}

static uint32_t taskbarwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix ) {
    uint32_t space = w->bar->center_widget_space;
    if (space == 0) return 0;

    awl_plugin_data_t* P = awl_plugin_get();
    if (!P) return 0;

    int n_windows = w->bar->n_tagwindows;
    if (n_windows <= 0) {
        TEXT( space, "", P->awl_colors.fg_win, (pixman_color_t){0} );
        return 0;
    }

    // calculate space per window
    uint32_t spaces[n_windows];
    for (int wi=0; wi<n_windows; ++wi)
        spaces[wi] = 0;
    for (uint32_t s=0; s<space; ++s)
        spaces[s%n_windows]++;
    int nospace = 0;
    for (int wi=0; wi<n_windows; ++wi)
        if (spaces[wi] <= 20)
            nospace = 1;

    if (nospace)
        return TEXT( space, "+++", P->awl_colors.fg_win, P->awl_colors.bg_win_urg );

    drwl_window_t* windows = w->bar->tagwindows;

    // max, float, top
    for (int wi=0; wi<n_windows; ++wi) {
        /* prefix is at most "[FMT] ", name is a fixed-size NUL-terminated
         * field -- size txt so both always fit */
        char txt[sizeof(windows[wi].name) + 8] = {0};
        if (windows[wi].floating || windows[wi].maximized || windows[wi].ontop) {
            strcat( txt, "[" );
            if (windows[wi].floating) strcat( txt, "F" );
            if (windows[wi].maximized) strcat( txt, "M" );
            if (windows[wi].ontop) strcat( txt, "T" );
            strcat( txt, "] " );
        }
        strcat( txt, windows[wi].name );
        TEXT( spaces[wi], txt, P->awl_colors.fg_win,
                windows[wi].focused ? P->awl_colors.bg_win_act :
                windows[wi].urgent  ? P->awl_colors.bg_win_urg :
                windows[wi].visible ? P->awl_colors.bg_win     :
                                      P->awl_colors.bg_win_min );
        x += spaces[wi];
    }
    return w->bar->center_widget_space;
}

static uint32_t layoutwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix ) {
    awl_plugin_data_t* P = awl_plugin_get();
    if (!P) return 0;
    int ww = TEXTW(w->bar->m, "XXX");
    TEXT( ww, w->bar->m->ltsymbol, P->awl_colors.fg_lay, P->awl_colors.bg_lay );
    return ww;
}

/* A right-hand widget showing one text: measure() fills it in, then
 * textsnap_draw() renders exactly that, so width and pixels always agree. */
typedef struct {
    char text[128];
    pixman_color_t fg, bg;
} textsnap_t;

static textsnap_t* textsnap( widget_t* w ) {
    if (!w->userdata) w->userdata = calloc(1, sizeof(textsnap_t));
    return w->userdata;
}

static uint32_t textsnap_width( widget_t* w ) {
    textsnap_t* s = w->userdata;
    return s && *s->text ? TEXTW( w->bar->m, s->text ) : 0;
}

static uint32_t textsnap_draw( widget_t* w, uint32_t x, pixman_image_t* pix ) {
    textsnap_t* s = w->userdata;
    if (!s) return 0;
    TEXT( w->width, s->text, s->fg, s->bg );
    return w->width;
}

typedef struct {
    char timestr[16];
    int sec; // -1: unknown
} clockwidget_userdata_t;

static uint32_t clockwidget_measure( widget_t* w ) {
    if (!w->userdata) w->userdata = calloc(1, sizeof(clockwidget_userdata_t));
    clockwidget_userdata_t* u = w->userdata;
    if (!*u->timestr) {
        strcpy(u->timestr, "--:--");
        u->sec = -1;
    }
    awl_plugin_data_t* P = awl_plugin_get();
    if (P && !sem_timedwait_nano(&P->date->sem, 1e3)) {
        memcpy(u->timestr, P->date->s, 16);
        u->timestr[15] = 0;
        u->sec = P->date->sec;
        w->age = 0;
        sem_post(&P->date->sem);
    } else {
        w->age++;
    }
    // time text plus the seconds meter
    int mw = 3 * w->bar->m->wlr_output->scale + 0.5f;
    if (mw < 2) mw = 2;
    return TEXTW( w->bar->m, "--:--" ) + mw;
}

static uint32_t clockwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix ) {
    awl_plugin_data_t* P = awl_plugin_get();
    if (!P) return 0;
    clockwidget_userdata_t* u = w->userdata;

    int mw = 3 * w->bar->m->wlr_output->scale + 0.5f;
    if (mw < 2) mw = 2;
    int ww = w->width - mw;
    TEXT( ww, u->timestr, P->awl_colors.fg_lay, P->awl_colors.bg_lay );

    // seconds meter right of the time: empty at :00, full at :59
    int sec = u->sec;
    int bar_height = w->bar->m->b.height;
    int ydiv = sec < 0 ? bar_height : bar_height - (sec * bar_height + 29) / 59;
    pixman_box32_t bg = { .x1 = x+ww, .x2 = x+ww+mw, .y1 = 0, .y2 = ydiv },
                   fg = { .x1 = x+ww, .x2 = x+ww+mw, .y1 = ydiv, .y2 = bar_height };
    pixman_image_fill_boxes( PIXMAN_OP_SRC, pix, &P->awl_colors.bg_lay, 1, &bg );
    pixman_color_t fgcolor = color_8bit_to_16bit( molokai_green );
    pixman_image_fill_boxes( PIXMAN_OP_SRC, pix, &fgcolor, 1, &fg );
    return w->width;
}

static uint32_t systray_measure( widget_t* w ) {
    return awl_host->tray_width( w->bar->m->wlr_output->name );
}

static uint32_t systray_draw( widget_t* w, uint32_t x, pixman_image_t* pix ) {
    // the tray is a separate window; put it where the bar left room, and
    // paint the slot in its color for the sub-pixel it doesn't cover
    awl_host->tray_set_widget_x( w->bar->m->wlr_output->name, x );
    pixman_color_t bg = color_8bit_to_16bit( AWL_TRAY_BG );
    pixman_image_fill_boxes( PIXMAN_OP_SRC, pix, &bg, 1, &(pixman_box32_t){
        .x1 = x, .x2 = x + w->width, .y1 = 0, .y2 = w->bar->m->b.height } );
    return w->width;
}

static uint32_t pulsewidget_measure( widget_t* w ) {
    awl_plugin_data_t* P = awl_plugin_get();
    if (!P || !P->pulse) return 0;
    textsnap_t* s = textsnap( w );
    float val = atomic_load( &P->pulse->value );
    int muted = atomic_load( &P->pulse->muted );
    snprintf( s->text, sizeof(s->text), "♫%3.0f%%", val * 100.0f );
    s->fg = muted ? color_8bit_to_16bit(molokai_orange) :
            lround(val*100.0) > 100 ? color_8bit_to_16bit(molokai_red) :
                                      P->awl_colors.fg_lay;
    s->bg = P->awl_colors.bg_lay;
    // fixed width so the bar doesn't jitter while the volume changes
    return TEXTW( w->bar->m, "V___%" );
}

typedef struct {
    awl_stats_t stats;
    pixman_box32_t boxes[128*3*2];
} statuswidget_userdata_t;

static uint32_t statuswidget_measure( widget_t* w ) {
    awl_plugin_data_t* P = awl_plugin_get();
    if (!P || !P->stats) return 0;

    if (!w->userdata)
        w->userdata = calloc(1, sizeof(statuswidget_userdata_t));
    statuswidget_userdata_t* u = w->userdata;
    awl_stats_t* st = &u->stats;

    if (!sem_timedwait_nano( &P->stats->sem, 1e3 )) {
        w->age = 0;
        memcpy( st, P->stats, sizeof(awl_stats_t) );
        sem_post( &P->stats->sem );
    } else {
        w->age++;
    }

    // one column per sample, accumulated the same way draw() places them
    float xx = 0;
    int n = st->ncpu + st->nmem + st->nswp;
    for (int i=0; i<n; ++i) xx += w->bar->m->wlr_output->scale;
    return xx;
}

static uint32_t statuswidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix ) {
    awl_plugin_data_t* P = awl_plugin_get();
    if (!P) return 0;
    statuswidget_userdata_t* u = w->userdata;
    pixman_box32_t* widget_boxes = u->boxes;
    awl_stats_t* st = &u->stats;

    const int ncpu = st->ncpu,
              nmem = st->nmem,
              nswp = st->nswp;
    const float *icpu = st->cpu,
                *imem = st->mem,
                *iswp = st->swp;
    pixman_box32_t *b_cpu = widget_boxes;
    pixman_box32_t *b_mem = b_cpu + ncpu;
    pixman_box32_t *b_swp = b_mem + nmem;
    pixman_box32_t *b_bg = b_swp + nswp;
    pixman_box32_t *b_bg_run = b_bg;

    int bar_height = w->bar->m->b.height;
    float xx=0;
    if (icpu) {
        for (int i=0; i<ncpu; ++i) {
            int ydiv = bar_height - icpu[st->dir ? ncpu-i : i] * bar_height;
            float next_x = xx + w->bar->m->wlr_output->scale;
            *b_bg_run++ = (pixman_box32_t){.x1=x+(int32_t)xx,.x2=x+(int32_t)next_x,.y1=0, .y2=ydiv};
            b_cpu[i] = (pixman_box32_t){.x1=x+(int32_t)xx,.x2=x+(int32_t)next_x,.y1=ydiv,.y2=bar_height};
            xx = next_x;
        }
    }
    if (imem) {
        for (int i=0; i<nmem; ++i) {
            int ydiv = bar_height - imem[st->dir ? ncpu-i : i] * bar_height;
            float next_x = xx + w->bar->m->wlr_output->scale;
            *b_bg_run++ = (pixman_box32_t){.x1=x+(int32_t)xx,.x2=x+(int32_t)next_x,.y1=0, .y2=ydiv};
            b_mem[i] = (pixman_box32_t){.x1=x+(int32_t)xx,.x2=x+(int32_t)next_x,.y1=ydiv,.y2=bar_height};
            xx = next_x;
        }
    }
    if (iswp) {
        for (int i=0; i<nswp; ++i) {
            int ydiv = bar_height - iswp[st->dir ? ncpu-i : i] * bar_height;
            float next_x = xx + w->bar->m->wlr_output->scale;
            *b_bg_run++ = (pixman_box32_t){.x1=x+(int32_t)xx,.x2=x+(int32_t)next_x,.y1=0, .y2=ydiv};
            b_swp[i] = (pixman_box32_t){.x1=x+(int32_t)xx,.x2=x+(int32_t)next_x,.y1=ydiv,.y2=bar_height};
            xx = next_x;
        }
    }

    pixman_color_t bgcolor = P->awl_colors.bg_stats;
    for (int a=0; a<w->age; ++a) bgcolor = mean_color_16( bgcolor, P->awl_colors.bg_status, 0.9 );

    pixman_image_fill_boxes(PIXMAN_OP_SRC, pix, &bgcolor, b_bg_run-b_bg, b_bg);
    pixman_image_fill_boxes(PIXMAN_OP_SRC, pix, &P->awl_colors.fg_stats_cpu, ncpu, b_cpu);
    pixman_image_fill_boxes(PIXMAN_OP_SRC, pix, &P->awl_colors.fg_stats_mem, nmem, b_mem);
    pixman_image_fill_boxes(PIXMAN_OP_SRC, pix, &P->awl_colors.fg_stats_swp, nswp, b_swp);

    return w->width;
}

/* text of sensor i; the same snapshot always gives the same text */
static void tempwidget_text( awl_temperature_t* T, int i, char* text, size_t size ) {
    // only put the label if the string is set
    if (*T->f_labels[T->idx[i]])
        snprintf( text, size, "%s:%.0f°C", T->f_labels[T->idx[i]], T->temps[i] );
    else
        snprintf( text, size, "%3.0f°C", T->temps[i] );
}

static uint32_t tempwidget_measure( widget_t* w ) {
    awl_plugin_data_t* P = awl_plugin_get();
    if (!P || !P->temp) return 0;

    if (!w->userdata) w->userdata = calloc(1,sizeof(awl_temperature_t));
    awl_temperature_t* T = w->userdata;

    // never block the compositor; the plugin requests a redraw after each
    // change, so a missed lock is caught up on the next frame
    if (!sem_trywait( &P->temp->sem )) {
        w->age = 0;
        memcpy( T, P->temp, sizeof(awl_temperature_t) );
        sem_post( &P->temp->sem );
    } else {
        w->age++;
    }

    uint32_t width = 0;
    for (int i=0; i<T->ntemps; ++i) {
        char text[128];
        tempwidget_text( T, i, text, sizeof(text) );
        width += TEXTW(w->bar->m, text);
    }
    return width;
}

static uint32_t tempwidget_draw( widget_t* w, uint32_t x, pixman_image_t* pix ) {
    awl_plugin_data_t* P = awl_plugin_get();
    if (!P) return 0;
    awl_temperature_t* T = w->userdata;

    pixman_color_t bgcolor = P->awl_colors.bg_status;
    for (int a=0; a<w->age; ++a) bgcolor = mean_color_16( bgcolor, white, 0.9 );

    for (int i=0; i<T->ntemps; ++i) {
        char text[128];
        tempwidget_text( T, i, text, sizeof(text) );
        pixman_color_t fgcolor = color_8bit_to_16bit(
                P->temp_color( T->temps[i], T->f_t_min[T->idx[i]], T->f_t_max[T->idx[i]] ) );
        uint32_t ww = TEXTW(w->bar->m, text);
        TEXT( ww, text, fgcolor, bgcolor );
        x += ww;
    }
    return w->width;
}

static uint32_t batwidget_measure( widget_t* w ) {
    awl_plugin_data_t* P = awl_plugin_get();
    if (!P || !P->bat) return 0;

    int charging = atomic_load( &P->bat->charging );
    float charge = atomic_load( &P->bat->charge );
    if (charging < 0) return 0;

    textsnap_t* s = textsnap( w );
    snprintf( s->text, sizeof(s->text), "%3.0f%%", charge * 100.0 );
    s->fg = P->awl_colors.fg_status;
    if (charge < 0.3)   s->fg = color_8bit_to_16bit( molokai_orange );
    if (charge < 0.15)  s->fg = color_8bit_to_16bit( molokai_red );
    if (charging)       s->fg = color_8bit_to_16bit( molokai_green );
    s->bg = P->awl_colors.bg_status;
    return textsnap_width( w );
}

static uint32_t ipwidget_measure( widget_t* w ) {
    awl_plugin_data_t* P = awl_plugin_get();
    if (!P || !P->ip) return 0;
    textsnap_t* s = textsnap( w );

    // never block the compositor; on a missed lock show the last address
    if (!sem_trywait( &P->ip->sem )) {
        w->age = 0;
        memcpy( s->text, P->ip->address, sizeof(s->text) );
        s->text[sizeof(s->text)-1] = 0;
        sem_post( &P->ip->sem );
    } else {
        w->age++;
    }
    if (!*s->text) strcpy( s->text, "    invalid   " );

    s->fg = atomic_load( &P->ip->is_online ) ? color_8bit_to_16bit(molokai_green) :
                                               color_8bit_to_16bit(molokai_red);
    s->bg = P->awl_colors.bg_status;
    for (int a=0; a<w->age; ++a) s->bg = mean_color_16( s->bg, white, 0.9 );
    return textsnap_width( w );
}

static void tagwidget_scroll( widget_t* w, uint32_t x, int amount ) {
    (void)w;
    (void)x;
    awl_host->actions->cycle_view( &(Arg){.i=amount} );
}
static void tagwidget_click( widget_t* w, uint32_t x, int button ) {
    int t = (double)x / (double)w->width * NTAGS;
    switch (button) {
        case BTN_LEFT: awl_host->actions->view( &(Arg){.ui = (1 << t)} ); break;
        case BTN_RIGHT: awl_host->actions->toggleview( &(Arg){.ui = (1 << t)} ); break;
        case BTN_MIDDLE: awl_host->actions->view( &(Arg){.ui = ~0} ); break;
        default: break;
    }
}

static void layoutwidget_scroll( widget_t* w, uint32_t x, int amount ) {
    (void)w; (void)x; awl_host->actions->cycle_layout( &(Arg){.i=amount} );
}
static void layoutwidget_click( widget_t* w, uint32_t x, int button ) {
    (void)w; (void)x; awl_host->actions->cycle_layout( &(Arg){.i=button==BTN_LEFT?1:-1} );
}

static void taskbarwidget_scroll( widget_t* w, uint32_t x, int amount ) {
    awl_host->actions->focusstack( &(Arg){.i=amount} );
}
static void taskbarwidget_click( widget_t* w, uint32_t x, int button ) {
    if (w->bar->n_tagwindows <= 0) return;
    drwl_window_t* windows = w->bar->tagwindows;
    int win_idx = (double)x / (double)w->width * (double)w->bar->n_tagwindows;
    if (win_idx >= w->bar->n_tagwindows || win_idx < 0) return;

    if (!windows[win_idx].visible) {
        windows[win_idx].c->isvisible = 1;
        awl_host->focusclient(windows[win_idx].c, 1);
        goto arrange;
    }
    if (!windows[win_idx].focused) {
        awl_host->focusclient(windows[win_idx].c, 1);
        goto arrange;
    }
    windows[win_idx].c->isvisible = 0;
arrange:
    awl_host->arrange(windows[win_idx].c->mon);
}

static void pulsewidget_click( widget_t* w, uint32_t x, int button ) {
    (void)w; (void)x; (void)button;
    awl_host->actions->spawn( &(Arg){.v=(const char*[]){"pavucontrol", NULL}} );
}
static void pulsewidget_scroll( widget_t* w, uint32_t x, int amount ) {
    (void)w; (void)x;
    if (amount < 0) {
        awl_host->actions->spawn( &(Arg){.v=(const char*[]){"pactl", "set-sink-volume", "@DEFAULT_SINK@", "+2.5%", NULL }} );
    } else {
        awl_host->actions->spawn( &(Arg){.v=(const char*[]){"pactl", "set-sink-volume", "@DEFAULT_SINK@", "-2.5%", NULL }} );
    }
}

static void clockwidget_click( widget_t* w, uint32_t x, int button ) {
    (void)x; (void)button;
    awl_host->calendar_toggle( w->bar->m->wlr_output->name );
}

static void clockwidget_hover( widget_t* w ) {
    awl_host->calendar_show( w->bar->m->wlr_output->name );
}

static void clockwidget_leave( widget_t* w ) {
    (void)w;
    awl_host->calendar_hide();
}

static uint32_t backlightwidget_measure( widget_t* w ) {
    awl_plugin_data_t* P = awl_plugin_get();
    if (!P || !P->backlight) return 0;

    const int enabled = atomic_load( &P->backlight->enabled );
    textsnap_t* s = textsnap( w );
    strcpy( s->text, enabled ? "BR" : "DK" );
    s->fg = enabled ? P->awl_colors.fg_status : color_8bit_to_16bit(molokai_orange);
    s->bg = P->awl_colors.bg_status;
    return textsnap_width( w );
}

static void backlightwidget_click( widget_t* w, uint32_t x, int button ) {
    (void)w; (void)x;
    if (button == BTN_LEFT)
        // the timer only fires on the quarter hour; run the service once now
        awl_host->actions->spawn( &(Arg){.v=(const char*[]){"systemctl", "--user", "--no-block", "start",
                "backlight-tooler.timer", "backlight-tooler.service", NULL}} );
    else
        awl_host->actions->spawn( &(Arg){.v=(const char*[]){"systemctl", "--user", "stop",  "backlight-tooler.timer", NULL}} );
}
