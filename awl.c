#include "awl.h"
#include "awl_util.h"
#include "awl_client.h"
#include "awl_title.h"
#include "awl_log.h"

#include "awl_state.h"
#include "awl_extension.h"

#include "awl-ipc-unstable-v2-protocol.h"

static awl_config_t* C = NULL;
static awl_state_t* B = NULL;
static awl_vtable_t* V = NULL;
static awl_extension_t* E = NULL;

static void defer_reload_fun(const Arg* arg);
static void plugin_reload(void);
static void plugin_init(const char* lib);
static void plugin_free(void);

static int awl_ready = 0;
int awl_is_ready( void ) { return awl_ready; }

static int defer_reload = 0;
static int log_level = WLR_ERROR;
static Key essential_keys[] = {
    { AWL_MODKEY|WLR_MODIFIER_SHIFT,         XKB_KEY_equal,            quit,             {0} },
    { AWL_MODKEY|WLR_MODIFIER_CTRL,          XKB_KEY_r,                defer_reload_fun, {0} },
    { WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_Terminate_Server, quit,             {0} },
#define CHVT(n) { WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_XF86Switch_VT_##n, chvt, {.ui = (n)} }
    CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
    CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};
static const int n_essential_keys = LENGTH(essential_keys);

static uint32_t awl_last_modkey = AWL_MODKEY;
void awl_change_modkey( uint32_t modkey ) {
    for (int i=0; i<n_essential_keys; ++i)
        if (essential_keys[i].mod & awl_last_modkey) {
            essential_keys[i].mod &= ~awl_last_modkey;
            essential_keys[i].mod |= modkey;
        }
    awl_last_modkey = modkey;
}

/* macros */
static inline int MIN( int A, int B ) { return A < B ? A : B; }
static inline int MAX( int A, int B ) { return A > B ? A : B; }
#define IDLE_NOTIFY_ACTIVITY \
    wlr_idle_notify_activity(B->idle, B->seat), wlr_idle_notifier_v1_notify_activity(B->idle_notifier, B->seat)

/* global event handlers */
struct wl_listener cursor_axis = {.notify = axisnotify};
struct wl_listener cursor_button = {.notify = buttonpress};
struct wl_listener cursor_frame = {.notify = cursorframe};
struct wl_listener cursor_motion = {.notify = motionrelative};
struct wl_listener cursor_motion_absolute = {.notify = motionabsolute};
struct wl_listener drag_icon_destroy = {.notify = destroydragicon};
struct zdwl_ipc_manager_v2_interface dwl_manager_implementation = {
    .release = dwl_ipc_manager_release,
    .get_output = dwl_ipc_manager_get_output
};
struct zdwl_ipc_output_v2_interface dwl_output_implementation = {
    .release = dwl_ipc_output_release,
    .set_tags = dwl_ipc_output_set_tags,
    .set_layout = dwl_ipc_output_set_layout,
    .set_client_tags = dwl_ipc_output_set_client_tags
};
struct wl_listener idle_inhibitor_create = {.notify = createidleinhibitor};
struct wl_listener idle_inhibitor_destroy = {.notify = destroyidleinhibitor};
struct wl_listener layout_change = {.notify = updatemons};
struct wl_listener new_input = {.notify = inputdevice};
struct wl_listener new_virtual_keyboard = {.notify = virtualkeyboard};
struct wl_listener new_output = {.notify = createmon};
struct wl_listener new_xdg_surface = {.notify = createnotify};
struct wl_listener new_xdg_decoration = {.notify = createdecoration};
struct wl_listener new_layer_shell_surface = {.notify = createlayersurface};
struct wl_listener output_mgr_apply = {.notify = outputmgrapply};
struct wl_listener output_mgr_test = {.notify = outputmgrtest};
struct wl_listener request_activate = {.notify = urgent};
struct wl_listener request_cursor = {.notify = setcursor};
struct wl_listener request_set_psel = {.notify = setpsel};
struct wl_listener request_set_sel = {.notify = setsel};
struct wl_listener request_start_drag = {.notify = requeststartdrag};
struct wl_listener start_drag = {.notify = startdrag};
struct wl_listener session_lock_create_lock = {.notify = locksession};
struct wl_listener session_lock_mgr_destroy = {.notify = destroysessionmgr};

struct wl_listener new_xwayland_surface = {.notify = createnotifyx11};
struct wl_listener xwayland_ready = {.notify = xwaylandready};
struct wlr_xwayland *xwayland;
xcb_atom_t netatom[NetLast];

/* function implementations */
void applybounds(Client *c, struct wlr_box *bbox) {
    if (!c->isfullscreen) {
        struct wlr_box min = {0}, max = {0};
        client_get_size_hints(c, &max, &min);
        /* try to set size hints */
        c->geom.width = MAX(min.width + (2 * (int)c->bw), c->geom.width);
        c->geom.height = MAX(min.height + (2 * (int)c->bw), c->geom.height);
        /* Some clients set their max size to INT_MAX, which does not violate the
         * protocol but it's unnecesary, as they can set their max size to zero. */
        if (max.width > 0 && !(2 * (int)c->bw > INT_MAX - max.width)) /* Checks for overflow */
            c->geom.width = MIN(max.width + (2 * c->bw), c->geom.width);
        if (max.height > 0 && !(2 * (int)c->bw > INT_MAX - max.height)) /* Checks for overflow */
            c->geom.height = MIN(max.height + (2 * c->bw), c->geom.height);
    }

    if (c->geom.x >= bbox->x + bbox->width)
        c->geom.x = bbox->x + bbox->width - c->geom.width;
    if (c->geom.y >= bbox->y + bbox->height)
        c->geom.y = bbox->y + bbox->height - c->geom.height;
    if (c->geom.x + c->geom.width + 2 * c->bw <= (unsigned)bbox->x)
        c->geom.x = bbox->x;
    if (c->geom.y + c->geom.height + 2 * c->bw <= (unsigned)bbox->y)
        c->geom.y = bbox->y;
}

void applyrules(Client *c) {
    /* rule matching */
    const char *appid, *title;
    uint32_t newtags = 0;
    const Rule *r;
    Monitor *mon = B->selmon, *m;

    c->isfloating = client_is_float_type(c);
    if (!(appid = client_get_appid(c)))
        appid = B->broken;
    if (!(title = client_get_title(c)))
        title = B->broken;

    for (r = C->rules; r < C->rules + C->n_rules; r++) {
        if ((!r->title || strstr(title, r->title))
                && (!r->id || strstr(appid, r->id))) {
            c->isfloating = r->isfloating;
            newtags |= r->tags;
            int i = 0;
            wl_list_for_each(m, &B->mons, link)
                if (r->monitor == i++)
                    mon = m;
        }
    }
    wlr_scene_node_reparent(&c->scene->node, B->layers[c->isfloating ? LyrFloat : LyrTile]);
    setmon(c, mon, newtags);
}

void arrange(Monitor *m) {
    Client *c;
    wl_list_for_each(c, &B->clients, link)
        if (c->mon == m)
            wlr_scene_node_set_enabled(&c->scene->node, VISIBLEON(c, m) && c->visible);

    wlr_scene_node_set_enabled(&m->fullscreen_bg->node,
            (c = focustop(m)) && c->isfullscreen);

    strncpy(m->ltsymbol, C->layouts[m->lt[m->sellt]].symbol, LENGTH(m->ltsymbol)-1);

    if (C->layouts[m->lt[m->sellt]].arrange)
        C->layouts[m->lt[m->sellt]].arrange(m);
    motionnotify(0);
    checkidleinhibitor(NULL);
}

void arrangelayer(Monitor *m, struct wl_list *list, struct wlr_box *usable_area, int exclusive) {
    LayerSurface *layersurface;
    struct wlr_box full_area = m->m;

    wl_list_for_each(layersurface, list, link) {
        struct wlr_layer_surface_v1 *wlr_layer_surface = layersurface->layer_surface;
        struct wlr_layer_surface_v1_state *state = &wlr_layer_surface->current;

        if (exclusive != (state->exclusive_zone > 0))
            continue;

        wlr_scene_layer_surface_v1_configure(layersurface->scene_layer, &full_area, usable_area);
        wlr_scene_node_set_position(&layersurface->popups->node,
                layersurface->scene->node.x, layersurface->scene->node.y);
        layersurface->geom.x = layersurface->scene->node.x;
        layersurface->geom.y = layersurface->scene->node.y;
    }
}

void arrangelayers(Monitor *m) {
    struct wlr_box usable_area = m->m;
    uint32_t layers_above_shell[] = {
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
    };
    LayerSurface *layersurface;
    if (!m->wlr_output->enabled)
        return;

    /* Arrange exclusive surfaces from top->bottom */
    for (int i = 3; i >= 0; i--)
        arrangelayer(m, &m->layers[i], &usable_area, 1);

    if (memcmp(&usable_area, &m->w, sizeof(struct wlr_box))) {
        m->w = usable_area;
        arrange(m);
    }

    /* Arrange non-exlusive surfaces from top->bottom */
    for (int i = 3; i >= 0; i--)
        arrangelayer(m, &m->layers[i], &usable_area, 0);

    /* Find topmost keyboard interactive layer, if such a layer exists */
    for (unsigned i = 0; i < LENGTH(layers_above_shell); i++) {
        wl_list_for_each_reverse(layersurface,
                &m->layers[layers_above_shell[i]], link) {
            if (!B->locked && layersurface->layer_surface->current.keyboard_interactive
                    && layersurface->mapped) {
                /* Deactivate the focused client. */
                focusclient(NULL, 0);
                B->exclusive_focus = layersurface;
                client_notify_enter(B, layersurface->layer_surface->surface, wlr_seat_get_keyboard(B->seat));
                return;
            }
        }
    }
}

void axisnotify(struct wl_listener *listener, void *data) {
    (void)listener;
    /* This event is forwarded by the cursor when a pointer emits an axis event,
     * for example when you move the scroll wheel. */
    struct wlr_pointer_axis_event *event = data;
    IDLE_NOTIFY_ACTIVITY;
    /* TODO: allow usage of scroll whell for mousebindings, it can be implemented
     * checking the event's orientation and the delta of the event */
    /* Notify the client with pointer focus of the axis event. */
    wlr_seat_pointer_notify_axis(B->seat,
            event->time_msec, event->orientation, event->delta,
            event->delta_discrete, event->source);
}

void buttonpress(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_pointer_button_event *event = data;
    struct wlr_keyboard *keyboard;
    uint32_t mods;
    Client *c;
    const Button *b;

    IDLE_NOTIFY_ACTIVITY;

    switch (event->state) {
    case WLR_BUTTON_PRESSED:
        B->cursor_mode = CurPressed;
        if (B->locked)
            break;

        /* Change focus if the button was _pressed_ over a client */
        xytonode(B->cursor->x, B->cursor->y, NULL, &c, NULL, NULL, NULL);
        if (c && (!client_is_unmanaged(c) || client_wants_focus(c)))
            focusclient(c, 1);

        keyboard = wlr_seat_get_keyboard(B->seat);
        mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
        for (b = C->buttons; b < C->buttons + C->n_buttons; b++) {
            if (CLEANMASK(mods) == CLEANMASK(b->mod) &&
                    event->button == b->button && b->func) {
                b->func(&b->arg);
                return;
            }
        }
        break;
    case WLR_BUTTON_RELEASED:
        /* If you released any buttons, we exit interactive move/resize mode. */
        if (!B->locked && B->cursor_mode != CurNormal && B->cursor_mode != CurPressed) {
            B->cursor_mode = CurNormal;
            /* Clear the pointer focus, this way if the cursor is over a surface
             * we will send an enter event after which the client will provide us
             * a cursor surface */
            wlr_seat_pointer_clear_focus(B->seat);
            motionnotify(0);
            /* Drop the window off on its new monitor */
            B->selmon = xytomon(B->cursor->x, B->cursor->y);
            setmon(B->grabc, B->selmon, 0);
            return;
        } else {
            B->cursor_mode = CurNormal;
        }
        break;
    }
    /* If the event wasn't handled by the compositor, notify the client with
     * pointer focus that a button press has occurred */
    wlr_seat_pointer_notify_button(B->seat,
            event->time_msec, event->button, event->state);
}

void chvt(const Arg *arg) {
    wlr_session_change_vt(wlr_backend_get_session(B->backend), arg->ui);
}

void checkidleinhibitor(struct wlr_surface *exclude) {
    int inhibited = 0, unused_lx, unused_ly;
    struct wlr_idle_inhibitor_v1 *inhibitor;
    wl_list_for_each(inhibitor, &B->idle_inhibit_mgr->inhibitors, link) {
        struct wlr_surface *surface = wlr_surface_get_root_surface(inhibitor->surface);
        struct wlr_scene_tree *tree = surface->data;
        if (exclude != surface && (C->bypass_surface_visibility || (!tree
                || wlr_scene_node_coords(&tree->node, &unused_lx, &unused_ly)))) {
            inhibited = 1;
            break;
        }
    }

    wlr_idle_set_enabled(B->idle, NULL, !inhibited);
    wlr_idle_notifier_v1_set_inhibited(B->idle_notifier, inhibited);
}

void cleanup(void) {
    awl_log_printf("cleanup awl");
    wlr_xwayland_destroy(xwayland);
    wl_display_destroy_clients(B->dpy);
    if (B->child_pid > 0) {
        kill(B->child_pid, SIGTERM);
        waitpid(B->child_pid, NULL, 0);
    }
    wlr_backend_destroy(B->backend);
    wlr_scene_node_destroy(&B->scene->tree.node);
    wlr_renderer_destroy(B->drw);
    wlr_allocator_destroy(B->alloc);
    wlr_xcursor_manager_destroy(B->cursor_mgr);
    wlr_cursor_destroy(B->cursor);
    wlr_output_layout_destroy(B->output_layout);
    wlr_seat_destroy(B->seat);
    wl_display_destroy(B->dpy);
}

void cleanupkeyboard(struct wl_listener *listener, void *data) {
    (void)data;
    Keyboard *kb = wl_container_of(listener, kb, destroy);

    wl_event_source_remove(kb->key_repeat_source);
    wl_list_remove(&kb->link);
    wl_list_remove(&kb->modifiers.link);
    wl_list_remove(&kb->key.link);
    wl_list_remove(&kb->destroy.link);
    free(kb);
}

void cleanupmon(struct wl_listener *listener, void *data) {
    (void)data;
    Monitor *m = wl_container_of(listener, m, destroy);
    LayerSurface *l, *tmp;
    int i;

    DwlIpcOutput *ipc_output, *ipc_output_tmp;
    wl_list_for_each_safe(ipc_output, ipc_output_tmp, &m->dwl_ipc_outputs, link)
        wl_resource_destroy(ipc_output->resource);
    for (i = 0; i <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; i++)
        wl_list_for_each_safe(l, tmp, &m->layers[i], link)
            wlr_layer_surface_v1_destroy(l->layer_surface);

    wl_list_remove(&m->destroy.link);
    wl_list_remove(&m->frame.link);
    wl_list_remove(&m->link);
    m->wlr_output->data = NULL;
    wlr_output_layout_remove(B->output_layout, m->wlr_output);
    wlr_scene_output_destroy(m->scene_output);
    wlr_scene_node_destroy(&m->fullscreen_bg->node);

    closemon(m);
    free(m);
}

void closemon(Monitor *m) {
    /* update selmon if needed and
     * move closed monitor's clients to the focused one */
    Client *c;
    if (wl_list_empty(&B->mons)) {
        B->selmon = NULL;
    } else if (m == B->selmon) {
        int nmons = wl_list_length(&B->mons), i = 0;
        do /* don't switch to disabled mons */
            B->selmon = wl_container_of(B->mons.next, B->selmon, link);
        while (!B->selmon->wlr_output->enabled && i++ < nmons);
    }

    wl_list_for_each(c, &B->clients, link) {
        if (c->isfloating && c->geom.x > m->m.width)
            resize(c, (struct wlr_box){.x = c->geom.x - m->w.width, .y = c->geom.y,
                .width = c->geom.width, .height = c->geom.height}, 0);
        if (c->mon == m)
            setmon(c, B->selmon, c->tags);
    }
    focusclient(focustop(B->selmon), 1);
    printstatus();
}

void commitlayersurfacenotify(struct wl_listener *listener, void *data) {
    (void)data;
    LayerSurface *layersurface = wl_container_of(listener, layersurface, surface_commit);
    struct wlr_layer_surface_v1 *wlr_layer_surface = layersurface->layer_surface;
    struct wlr_output *wlr_output = wlr_layer_surface->output;
    struct wlr_scene_tree *layer = B->layers[B->layermap[wlr_layer_surface->current.layer]];

    /* For some reason this layersurface have no monitor, this can be because
     * its monitor has just been destroyed */
    if (!wlr_output || !(layersurface->mon = wlr_output->data))
        return;

    if (layer != layersurface->scene->node.parent) {
        wlr_scene_node_reparent(&layersurface->scene->node, layer);
        wlr_scene_node_reparent(&layersurface->popups->node, layer);
        wl_list_remove(&layersurface->link);
        wl_list_insert(&layersurface->mon->layers[wlr_layer_surface->current.layer],
                &layersurface->link);
    }
    if (wlr_layer_surface->current.layer < ZWLR_LAYER_SHELL_V1_LAYER_TOP)
        wlr_scene_node_reparent(&layersurface->popups->node, B->layers[LyrTop]);

    if (wlr_layer_surface->current.committed == 0
            && layersurface->mapped == wlr_layer_surface->mapped)
        return;
    layersurface->mapped = wlr_layer_surface->mapped;

    arrangelayers(layersurface->mon);
}

void commitnotify(struct wl_listener *listener, void *data) {
    (void)data;
    Client *c = wl_container_of(listener, c, commit);

    /* mark a pending resize as completed */
    if (c->resize && c->resize <= c->surface.xdg->current.configure_serial)
        c->resize = 0;
}

void createdecoration(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_xdg_toplevel_decoration_v1 *dec = data;
    wlr_xdg_toplevel_decoration_v1_set_mode(dec, WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void createidleinhibitor(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_idle_inhibitor_v1 *idle_inhibitor = data;
    wl_signal_add(&idle_inhibitor->events.destroy, &idle_inhibitor_destroy);

    checkidleinhibitor(NULL);
}

void createkeyboard(struct wlr_keyboard *keyboard) {
    struct xkb_context *context;
    struct xkb_keymap *keymap;
    Keyboard *kb = keyboard->data = ecalloc(1, sizeof(*kb));
    kb->wlr_keyboard = keyboard;

    /* Prepare an XKB keymap and assign it to the keyboard. */
    context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    keymap = xkb_keymap_new_from_names(context, &C->xkb_rules,
        XKB_KEYMAP_COMPILE_NO_FLAGS);

    wlr_keyboard_set_keymap(keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(keyboard, C->repeat_rate, C->repeat_delay);

    /* Here we set up listeners for keyboard events. */
    LISTEN(&keyboard->events.modifiers, &kb->modifiers, keypressmod);
    LISTEN(&keyboard->events.key, &kb->key, keypress);
    LISTEN(&keyboard->base.events.destroy, &kb->destroy, cleanupkeyboard);

    wlr_seat_set_keyboard(B->seat, keyboard);

    kb->key_repeat_source = wl_event_loop_add_timer(
            wl_display_get_event_loop(B->dpy), keyrepeat, kb);

    /* And add the keyboard to our list of keyboards */
    wl_list_insert(&B->keyboards, &kb->link);
}

void createlayersurface(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_layer_surface_v1 *wlr_layer_surface = data;
    LayerSurface *layersurface;
    struct wlr_layer_surface_v1_state old_state;
    struct wlr_scene_tree *l = B->layers[B->layermap[wlr_layer_surface->pending.layer]];

    if (!wlr_layer_surface->output)
        wlr_layer_surface->output = B->selmon ? B->selmon->wlr_output : NULL;

    if (!wlr_layer_surface->output) {
        wlr_layer_surface_v1_destroy(wlr_layer_surface);
        return;
    }

    layersurface = ecalloc(1, sizeof(LayerSurface));
    layersurface->type = LayerShell;
    LISTEN(&wlr_layer_surface->surface->events.commit,
            &layersurface->surface_commit, commitlayersurfacenotify);
    LISTEN(&wlr_layer_surface->events.destroy, &layersurface->destroy,
            destroylayersurfacenotify);
    LISTEN(&wlr_layer_surface->events.map, &layersurface->map,
            maplayersurfacenotify);
    LISTEN(&wlr_layer_surface->events.unmap, &layersurface->unmap,
            unmaplayersurfacenotify);

    layersurface->layer_surface = wlr_layer_surface;
    layersurface->mon = wlr_layer_surface->output->data;
    wlr_layer_surface->data = layersurface;

    layersurface->scene_layer = wlr_scene_layer_surface_v1_create(l, wlr_layer_surface);
    layersurface->scene = layersurface->scene_layer->tree;
    layersurface->popups = wlr_layer_surface->surface->data = wlr_scene_tree_create(l);

    layersurface->scene->node.data = layersurface;

    wl_list_insert(&layersurface->mon->layers[wlr_layer_surface->pending.layer],
            &layersurface->link);

    /* Temporarily set the layer's current state to pending
     * so that we can easily arrange it
     */
    old_state = wlr_layer_surface->current;
    wlr_layer_surface->current = wlr_layer_surface->pending;
    layersurface->mapped = 1;
    arrangelayers(layersurface->mon);
    wlr_layer_surface->current = old_state;
}

void createlocksurface(struct wl_listener *listener, void *data) {
    SessionLock *lock = wl_container_of(listener, lock, new_surface);
    struct wlr_session_lock_surface_v1 *lock_surface = data;
    Monitor *m = lock_surface->output->data;
    struct wlr_scene_tree *scene_tree = lock_surface->surface->data =
        wlr_scene_subsurface_tree_create(lock->scene, lock_surface->surface);
    m->lock_surface = lock_surface;

    wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
    wlr_session_lock_surface_v1_configure(lock_surface, m->m.width, m->m.height);

    LISTEN(&lock_surface->events.destroy, &m->destroy_lock_surface, destroylocksurface);

    if (m == B->selmon)
        client_notify_enter(B, lock_surface->surface, wlr_seat_get_keyboard(B->seat));
}

void createmon(struct wl_listener *listener, void *data) {
    (void)listener;
    /* This event is raised by the backend when a new output (aka a display or
     * monitor) becomes available. */
    struct wlr_output *wlr_output = data;
    const MonitorRule *r;
    size_t i;
    Monitor *m = wlr_output->data = ecalloc(1, sizeof(*m));
    m->wlr_output = wlr_output;

    wl_list_init(&m->dwl_ipc_outputs);
    wlr_output_init_render(wlr_output, B->alloc, B->drw);

    /* Initialize monitor state using configured rules */
    for (i = 0; i < LENGTH(m->layers); i++)
        wl_list_init(&m->layers[i]);
    m->tagset[0] = m->tagset[1] = 1;
    for (r = C->monrules; r < C->monrules + C->n_monrules; r++) {
        if (!r->name || strstr(wlr_output->name, r->name)) {
            m->mfact = r->mfact;
            m->nmaster = r->nmaster;
            wlr_output_set_scale(wlr_output, r->scale);
            wlr_xcursor_manager_load(B->cursor_mgr, r->scale);
            m->lt[0] = m->lt[1] = r->lt;
            wlr_output_set_transform(wlr_output, r->rr);
            m->m.x = r->x;
            m->m.y = r->y;
            break;
        }
    }

    /* The mode is a tuple of (width, height, refresh rate), and each
     * monitor supports only a specific set of modes. We just pick the
     * monitor's preferred mode; a more sophisticated compositor would let
     * the user configure it. */
    wlr_output_set_mode(wlr_output, wlr_output_preferred_mode(wlr_output));

    /* Set up event listeners */
    LISTEN(&wlr_output->events.frame, &m->frame, rendermon);
    LISTEN(&wlr_output->events.destroy, &m->destroy, cleanupmon);

    wlr_output_enable(wlr_output, 1);
    if (!wlr_output_commit(wlr_output))
        return;

    /* Try to enable adaptive sync, note that not all monitors support it.
     * wlr_output_commit() will deactivate it in case it cannot be enabled */
    wlr_output_enable_adaptive_sync(wlr_output, 1);
    wlr_output_commit(wlr_output);

    wl_list_insert(&B->mons, &m->link);
    printstatus();

    /* The xdg-protocol specifies:
     *
     * If the fullscreened surface is not opaque, the compositor must make
     * sure that other screen content not part of the same surface tree (made
     * up of subsurfaces, popups or similarly coupled surfaces) are not
     * visible below the fullscreened surface.
     *
     */
    /* updatemons() will resize and set correct position */
    m->fullscreen_bg = wlr_scene_rect_create(B->layers[LyrFS], 0, 0, C->fullscreen_bg);
    wlr_scene_node_set_enabled(&m->fullscreen_bg->node, 0);

    /* Adds this to the output layout in the order it was configured in.
     *
     * The output layout utility automatically adds a wl_output global to the
     * display, which Wayland clients can see to find out information about the
     * output (such as DPI, scale factor, manufacturer, etc).
     */
    m->scene_output = wlr_scene_output_create(B->scene, wlr_output);
    if (m->m.x < 0 || m->m.y < 0)
        wlr_output_layout_add_auto(B->output_layout, wlr_output);
    else
        wlr_output_layout_add(B->output_layout, wlr_output, m->m.x, m->m.y);
    strncpy(m->ltsymbol, C->layouts[m->lt[m->sellt]].symbol, LENGTH(m->ltsymbol)-1);
}

void createnotify(struct wl_listener *listener, void *data) {
    (void)listener;
    /* This event is raised when wlr_xdg_shell receives a new xdg surface from a
     * client, either a toplevel (application window) or popup,
     * or when wlr_layer_shell receives a new popup from a layer.
     * If you want to do something tricky with popups you should check if
     * its parent is wlr_xdg_shell or wlr_layer_shell */
    struct wlr_xdg_surface *xdg_surface = data;
    Client *c = NULL;
    LayerSurface *l = NULL;

    if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
        struct wlr_box box;
        int type = toplevel_from_wlr_surface(xdg_surface->surface, &c, &l);
        if (!xdg_surface->popup->parent || type < 0)
            return;
        xdg_surface->surface->data = wlr_scene_xdg_surface_create(
                xdg_surface->popup->parent->data, xdg_surface);
        if ((l && !l->mon) || (c && !c->mon))
            return;
        box = type == LayerShell ? l->mon->m : c->mon->w;
        box.x -= (type == LayerShell ? l->geom.x : c->geom.x);
        box.y -= (type == LayerShell ? l->geom.y : c->geom.y);
        wlr_xdg_popup_unconstrain_from_box(xdg_surface->popup, &box);
        return;
    } else if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_NONE)
        return;

    /* Allocate a Client for this surface */
    c = xdg_surface->data = ecalloc(1, sizeof(*c));
    c->surface.xdg = xdg_surface;
    c->bw = C->borderpx;
    c->visible = 1;

    LISTEN(&xdg_surface->events.map, &c->map, mapnotify);
    LISTEN(&xdg_surface->events.unmap, &c->unmap, unmapnotify);
    LISTEN(&xdg_surface->events.destroy, &c->destroy, destroynotify);
    LISTEN(&xdg_surface->toplevel->events.set_title, &c->set_title, updatetitle);
    LISTEN(&xdg_surface->toplevel->events.request_fullscreen, &c->fullscreen,
            fullscreennotify);
    LISTEN(&xdg_surface->toplevel->events.request_maximize, &c->maximize,
            maximizenotify);
}

void createpointer(struct wlr_pointer *pointer) {
    if (wlr_input_device_is_libinput(&pointer->base)) {
        struct libinput_device *libinput_device = (struct libinput_device*)
            wlr_libinput_get_device_handle(&pointer->base);

        if (libinput_device_config_tap_get_finger_count(libinput_device)) {
            libinput_device_config_tap_set_enabled(libinput_device, C->tap_to_click);
            libinput_device_config_tap_set_drag_enabled(libinput_device, C->tap_and_drag);
            libinput_device_config_tap_set_drag_lock_enabled(libinput_device, C->drag_lock);
            libinput_device_config_tap_set_button_map(libinput_device, C->button_map);
        }

        if (libinput_device_config_scroll_has_natural_scroll(libinput_device))
            libinput_device_config_scroll_set_natural_scroll_enabled(libinput_device, C->natural_scrolling);

        if (libinput_device_config_dwt_is_available(libinput_device))
            libinput_device_config_dwt_set_enabled(libinput_device, C->disable_while_typing);

        if (libinput_device_config_left_handed_is_available(libinput_device))
            libinput_device_config_left_handed_set(libinput_device, C->left_handed);

        if (libinput_device_config_middle_emulation_is_available(libinput_device))
            libinput_device_config_middle_emulation_set_enabled(libinput_device, C->middle_button_emulation);

        if (libinput_device_config_scroll_get_methods(libinput_device) != LIBINPUT_CONFIG_SCROLL_NO_SCROLL)
            libinput_device_config_scroll_set_method (libinput_device, C->scroll_method);

        if (libinput_device_config_click_get_methods(libinput_device) != LIBINPUT_CONFIG_CLICK_METHOD_NONE)
            libinput_device_config_click_set_method (libinput_device, C->click_method);

        if (libinput_device_config_send_events_get_modes(libinput_device))
            libinput_device_config_send_events_set_mode(libinput_device, C->send_events_mode);

        if (libinput_device_config_accel_is_available(libinput_device)) {
            libinput_device_config_accel_set_profile(libinput_device, C->accel_profile);
            libinput_device_config_accel_set_speed(libinput_device, C->accel_speed);
        }
    }

    wlr_cursor_attach_input_device(B->cursor, &pointer->base);
}

void cursorframe(struct wl_listener *listener, void *data) {
    (void)listener;
    (void)data;
    /* This event is forwarded by the cursor when a pointer emits an frame
     * event. Frame events are sent after regular pointer events to group
     * multiple events together. For instance, two axis events may happen at the
     * same time, in which case a frame event won't be sent in between. */
    /* Notify the client with pointer focus of the frame event. */
    wlr_seat_pointer_notify_frame(B->seat);
}

void destroydragicon(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_drag_icon *icon = data;
    wlr_scene_node_destroy(icon->data);
    /* Focus enter isn't sent during drag, so refocus the focused node. */
    focusclient(focustop(B->selmon), 1);
    motionnotify(0);
}

void destroyidleinhibitor(struct wl_listener *listener, void *data) {
    (void)listener;
    /* `data` is the wlr_surface of the idle inhibitor being destroyed,
     * at this point the idle inhibitor is still in the list of the manager */
    checkidleinhibitor(wlr_surface_get_root_surface(data));
}

void destroylayersurfacenotify(struct wl_listener *listener, void *data) {
    (void)data;
    LayerSurface *layersurface = wl_container_of(listener, layersurface, destroy);

    wl_list_remove(&layersurface->link);
    wl_list_remove(&layersurface->destroy.link);
    wl_list_remove(&layersurface->map.link);
    wl_list_remove(&layersurface->unmap.link);
    wl_list_remove(&layersurface->surface_commit.link);
    wlr_scene_node_destroy(&layersurface->scene->node);
    free(layersurface);
}

void destroylock(SessionLock *lock, int unlock) {
    wlr_seat_keyboard_notify_clear_focus(B->seat);
    if ((B->locked = !unlock))
        goto destroy;

    wlr_scene_node_set_enabled(&B->locked_bg->node, 0);

    focusclient(focustop(B->selmon), 0);
    motionnotify(0);

destroy:
    wl_list_remove(&lock->new_surface.link);
    wl_list_remove(&lock->unlock.link);
    wl_list_remove(&lock->destroy.link);

    wlr_scene_node_destroy(&lock->scene->node);
    B->cur_lock = NULL;
    free(lock);
}

void destroylocksurface(struct wl_listener *listener, void *data) {
    (void)data;
    Monitor *m = wl_container_of(listener, m, destroy_lock_surface);
    struct wlr_session_lock_surface_v1 *surface, *lock_surface = m->lock_surface;

    m->lock_surface = NULL;
    wl_list_remove(&m->destroy_lock_surface.link);

    if (lock_surface->surface != B->seat->keyboard_state.focused_surface)
        return;

    if (B->locked && B->cur_lock && !wl_list_empty(&B->cur_lock->surfaces)) {
        surface = wl_container_of(B->cur_lock->surfaces.next, surface, link);
        client_notify_enter(B, surface->surface, wlr_seat_get_keyboard(B->seat));
    } else if (!B->locked) {
        focusclient(focustop(B->selmon), 1);
    } else {
        wlr_seat_keyboard_clear_focus(B->seat);
    }
}

void destroynotify(struct wl_listener *listener, void *data) {
    (void)data;
    /* Called when the surface is destroyed and should never be shown again. */
    Client *c = wl_container_of(listener, c, destroy);
    wl_list_remove(&c->map.link);
    wl_list_remove(&c->unmap.link);
    wl_list_remove(&c->destroy.link);
    wl_list_remove(&c->set_title.link);
    wl_list_remove(&c->fullscreen.link);
    if (c->type != XDGShell) {
        wl_list_remove(&c->configure.link);
        wl_list_remove(&c->set_hints.link);
        wl_list_remove(&c->activate.link);
    }
    free(c);
}

void destroysessionlock(struct wl_listener *listener, void *data) {
    (void)data;
    SessionLock *lock = wl_container_of(listener, lock, destroy);
    destroylock(lock, 0);
}

void destroysessionmgr(struct wl_listener *listener, void *data) {
    (void)listener;
    (void)data;
    wl_list_remove(&session_lock_create_lock.link);
    wl_list_remove(&session_lock_mgr_destroy.link);
}

Monitor * dirtomon(enum wlr_direction dir) {
    struct wlr_output *next;
    if (!wlr_output_layout_get(B->output_layout, B->selmon->wlr_output))
        return B->selmon;
    if ((next = wlr_output_layout_adjacent_output(B->output_layout,
            dir, B->selmon->wlr_output, B->selmon->m.x, B->selmon->m.y)))
        return next->data;
    if ((next = wlr_output_layout_farthest_output(B->output_layout,
            dir ^ (WLR_DIRECTION_LEFT|WLR_DIRECTION_RIGHT),
            B->selmon->wlr_output, B->selmon->m.x, B->selmon->m.y)))
        return next->data;
    return B->selmon;
}

void dwl_ipc_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    (void)data;
    struct wl_resource *manager_resource = wl_resource_create(client, &zdwl_ipc_manager_v2_interface,
            version, id);
    if (!manager_resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(manager_resource, &dwl_manager_implementation, NULL, dwl_ipc_manager_destroy);

    zdwl_ipc_manager_v2_send_tags(manager_resource, TAGCOUNT);

    for (int i = 0; i < C->n_layouts; i++)
        zdwl_ipc_manager_v2_send_layout(manager_resource, C->layouts[i].symbol);
}

void dwl_ipc_manager_destroy(struct wl_resource *resource) {
    (void)resource;
    /* No state to destroy */
}

void dwl_ipc_manager_get_output(struct wl_client *client, struct wl_resource *resource,
        uint32_t id, struct wl_resource *output) {
    DwlIpcOutput *ipc_output;
    Monitor *monitor = wlr_output_from_resource(output)->data;
    struct wl_resource *output_resource = wl_resource_create(client, &zdwl_ipc_output_v2_interface,
            wl_resource_get_version(resource), id);
    if (!output_resource)
        return;

    ipc_output = ecalloc(1, sizeof(*ipc_output));
    ipc_output->resource = output_resource;
    ipc_output->mon = monitor;
    wl_resource_set_implementation(output_resource, &dwl_output_implementation, ipc_output,
            dwl_ipc_output_destroy);
    wl_list_insert(&monitor->dwl_ipc_outputs, &ipc_output->link);
    dwl_ipc_output_printstatus_to(ipc_output);
}

void dwl_ipc_manager_release(struct wl_client *client, struct wl_resource *resource) {
    (void)client;
    wl_resource_destroy(resource);
}

void dwl_ipc_output_destroy(struct wl_resource *resource) {
    DwlIpcOutput *ipc_output = wl_resource_get_user_data(resource);
    wl_list_remove(&ipc_output->link);
    free(ipc_output);
}

void dwl_ipc_output_printstatus(Monitor *monitor) {
    DwlIpcOutput *ipc_output;
    wl_list_for_each(ipc_output, &monitor->dwl_ipc_outputs, link)
        dwl_ipc_output_printstatus_to(ipc_output);
}

void dwl_ipc_output_printstatus_to(DwlIpcOutput *ipc_output) {
    Monitor *monitor = ipc_output->mon;
    Client *c, *focused;
    int tagmask, state, numclients, focused_client, tag;
    const char *appid;
    focused = focustop(monitor);
    zdwl_ipc_output_v2_send_active(ipc_output->resource, monitor == B->selmon);

    struct wl_array titles;
    wl_array_init(&titles);
    for (tag = 0 ; tag < TAGCOUNT; tag++) {
        numclients = state = focused_client = 0;
        tagmask = 1 << tag;
        if ((tagmask & monitor->tagset[monitor->seltags]) != 0)
            state |= ZDWL_IPC_OUTPUT_V2_TAG_STATE_ACTIVE;

        wl_list_for_each(c, &B->clients, link) {
            if (c->mon != monitor)
                continue;
            if (!(c->tags & tagmask))
                continue;
            if (c == focused)
                focused_client = 1;
            if (c->isurgent)
                state |= ZDWL_IPC_OUTPUT_V2_TAG_STATE_URGENT;

            if (c->tags & monitor->tagset[monitor->seltags]) {
                awl_title_t* ttl = wl_array_add(&titles, sizeof(awl_title_t));

                memset(ttl, 0, sizeof(awl_title_t));

                const char* t = client_get_title(c);
                if (!t) t = B->broken;

                ttl->name[sizeof(ttl->name)-1] = '\0';
                memcpy( ttl->name, t, MIN(strlen(t)+1, sizeof(ttl->name)-1) );

                ttl->floating = c->isfloating;
                ttl->focused = (c == focused);
                ttl->urgent = c->isurgent;
                ttl->visible = c->visible;
            }
            numclients++;
        }
        zdwl_ipc_output_v2_send_tag(ipc_output->resource, tag, state, numclients, focused_client);
    }
    appid = focused ? client_get_appid(focused) : "";

    zdwl_ipc_output_v2_send_layout(ipc_output->resource, monitor->lt[monitor->sellt]);
    zdwl_ipc_output_v2_send_title_ary(ipc_output->resource, &titles);
    wl_array_release(&titles);
    zdwl_ipc_output_v2_send_appid(ipc_output->resource, appid ? appid : B->broken);
    zdwl_ipc_output_v2_send_layout_symbol(ipc_output->resource, monitor->ltsymbol);
    if (wl_resource_get_version(ipc_output->resource) >= ZDWL_IPC_OUTPUT_V2_FULLSCREEN_SINCE_VERSION) {
        zdwl_ipc_output_v2_send_fullscreen(ipc_output->resource, focused ? focused->isfullscreen : 0);
    }
    if (wl_resource_get_version(ipc_output->resource) >= ZDWL_IPC_OUTPUT_V2_FLOATING_SINCE_VERSION) {
        zdwl_ipc_output_v2_send_floating(ipc_output->resource, focused ? focused->isfloating : 0);
    }
    zdwl_ipc_output_v2_send_frame(ipc_output->resource);
}

void dwl_ipc_output_set_client_tags(struct wl_client *client, struct wl_resource *resource,
        uint32_t and_tags, uint32_t xor_tags) {
    (void)client;
    DwlIpcOutput *ipc_output;
    Monitor *monitor;
    Client *selected_client;
    unsigned int newtags = 0;

    ipc_output = wl_resource_get_user_data(resource);
    if (!ipc_output)
        return;

    monitor = ipc_output->mon;
    selected_client = focustop(monitor);
    if (!selected_client)
        return;

    newtags = (selected_client->tags & and_tags) ^ xor_tags;
    if (!newtags)
        return;

    selected_client->tags = newtags;
    focusclient(focustop(B->selmon), 1);
    arrange(B->selmon);
    printstatus();
}

void dwl_ipc_output_set_layout(struct wl_client *client, struct wl_resource *resource, uint32_t index) {
    (void)client;
    DwlIpcOutput *ipc_output;

    ipc_output = wl_resource_get_user_data(resource);
    if (!ipc_output)
        return;
    if (index >= (unsigned)C->n_layouts)
        return;

    Arg A = {.i = index};
    setlayout(&A);
    printstatus();
}

void dwl_ipc_output_set_tags(struct wl_client *client, struct wl_resource *resource,
        uint32_t tagmask, uint32_t toggle_tagset) {
    (void)client;

    DwlIpcOutput *ipc_output;
    Monitor *monitor;
    unsigned int newtags = tagmask & TAGMASK;

    ipc_output = wl_resource_get_user_data(resource);
    if (!ipc_output)
        return;
    monitor = ipc_output->mon;

    if (!newtags || newtags == monitor->tagset[monitor->seltags])
        return;
    if (toggle_tagset)
        monitor->seltags ^= 1;

    monitor->tagset[monitor->seltags] = newtags;
    focusclient(focustop(monitor), 1);
    arrange(monitor);
    printstatus();
}

void dwl_ipc_output_release(struct wl_client *client, struct wl_resource *resource) {
    (void)client;
    wl_resource_destroy(resource);
}

void focusclient(Client *c, int lift) {
    struct wlr_surface *old = B->seat->keyboard_state.focused_surface;
    int unused_lx, unused_ly, old_client_type;
    Client *old_c = NULL;
    LayerSurface *old_l = NULL;

    if (B->locked)
        return;

    /* Raise client in stacking order if requested */
    if (c && lift)
        wlr_scene_node_raise_to_top(&c->scene->node);

    if (c && client_surface(c) == old)
        return;

    if ((old_client_type = toplevel_from_wlr_surface(old, &old_c, &old_l)) == XDGShell) {
        struct wlr_xdg_popup *popup, *tmp;
        wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link)
            wlr_xdg_popup_destroy(popup);
    }

    /* Put the new client atop the focus stack and select its monitor */
    if (c && !client_is_unmanaged(c)) {
        wl_list_remove(&c->flink);
        wl_list_insert(&B->fstack, &c->flink);
        B->selmon = c->mon;
        c->isurgent = 0;
        client_restack_surface(c);

        /* Don't change border color if there is an exclusive focus or we are
         * handling a drag operation */
        if (!B->exclusive_focus && !B->seat->drag)
            client_set_border_color(c, C->focuscolor);
    }

    /* Deactivate old client if focus is changing */
    if (old && (!c || client_surface(c) != old)) {
        /* If an overlay is focused, don't focus or activate the client,
         * but only update its position in fstack to render its border with C->focuscolor
         * and focus it after the overlay is closed. */
        if (old_client_type == LayerShell && wlr_scene_node_coords(
                    &old_l->scene->node, &unused_lx, &unused_ly)
                && old_l->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
            return;
        } else if (old_c && old_c == B->exclusive_focus && client_wants_focus(old_c)) {
            return;
        /* Don't deactivate old client if the new one wants focus, as this causes issues with winecfg
         * and probably other clients */
        } else if (old_c && !client_is_unmanaged(old_c) && (!c || !client_wants_focus(c))) {
            client_set_border_color(old_c, C->bordercolor);

            client_activate_surface(old, 0);
        }
    }
    printstatus();

    if (!c) {
        /* With no client, all we have left is to clear focus */
        wlr_seat_keyboard_notify_clear_focus(B->seat);
        return;
    }

    /* Change cursor surface */
    motionnotify(0);

    /* Have a client, so focus its top-level wlr_surface */
    client_notify_enter(B, client_surface(c), wlr_seat_get_keyboard(B->seat));

    /* Activate the new client */
    client_activate_surface(client_surface(c), 1);
}

void focusmon(const Arg *arg) {
    int i = 0, nmons = wl_list_length(&B->mons);
    if (nmons)
        do /* don't switch to disabled mons */
            B->selmon = dirtomon(arg->i);
        while (!B->selmon->wlr_output->enabled && i++ < nmons);
    focusclient(focustop(B->selmon), 1);
}

void focusstack(const Arg *arg) {
    /* Focus the next or previous client (in tiling order) on selmon */
    Client *c, *sel = focustop(B->selmon);
    if (!sel || sel->isfullscreen)
        return;
    if (arg->i > 0) {
        wl_list_for_each(c, &sel->link, link) {
            if (&c->link == &B->clients)
                continue; /* wrap past the sentinel node */
            if (VISIBLEON(c, B->selmon) && c->visible)
                break; /* found it */
        }
    } else {
        wl_list_for_each_reverse(c, &sel->link, link) {
            if (&c->link == &B->clients)
                continue; /* wrap past the sentinel node */
            if (VISIBLEON(c, B->selmon) && c->visible)
                break; /* found it */
        }
    }
    /* If only one client is visible on selmon, then c == sel */
    focusclient(c, 1);
}

/* We probably should change the name of this, it sounds like
 * will focus the topmost client of this mon, when actually will
 * only return that client */
Client * focustop(Monitor *m) {
    Client *c;
    wl_list_for_each(c, &B->fstack, flink)
        if (VISIBLEON(c, m) && c->visible)
            return c;
    return NULL;
}

void fullscreennotify(struct wl_listener *listener, void *data) {
    (void)data;
    Client *c = wl_container_of(listener, c, fullscreen);
    setfullscreen(c, client_wants_fullscreen(c));
}

void handlesig(int signo) {
    if (signo == SIGCHLD) {
        siginfo_t in;
        /* wlroots expects to reap the XWayland process itself, so we
         * use WNOWAIT to keep the child waitable until we know it's not
         * XWayland.
         */
        while (1) {
            int condition = !waitid(P_ALL, 0, &in, WEXITED|WNOHANG|WNOWAIT);
            if (condition) condition = in.si_pid;
            if (condition) condition = !xwayland;
            if (condition) condition = (in.si_pid != xwayland->server->pid);
            if (!condition) break;
            waitpid(in.si_pid, NULL, 0);
        }
    } else if (signo == SIGINT || signo == SIGTERM) {
        quit(NULL);
    }
}

void incnmaster(const Arg *arg) {
    if (!arg || !B->selmon)
        return;
    B->selmon->nmaster = MAX(B->selmon->nmaster + arg->i, 0);
    arrange(B->selmon);
}

void inputdevice(struct wl_listener *listener, void *data) {
    (void)listener;
    /* This event is raised by the backend when a new input device becomes
     * available. */
    struct wlr_input_device *device = data;
    uint32_t caps;

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        createkeyboard(wlr_keyboard_from_input_device(device));
        break;
    case WLR_INPUT_DEVICE_POINTER:
        createpointer(wlr_pointer_from_input_device(device));
        break;
    default:
        /* TODO handle other input device types */
        break;
    }

    /* We need to let the wlr_seat know what our capabilities are, which is
     * communiciated to the client. In dwl we always have a cursor, even if
     * there are no pointer devices, so we always include that capability. */
    /* TODO do we actually require a cursor? */
    caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&B->keyboards))
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(B->seat, caps);
}

int keybinding(uint32_t mods, xkb_keysym_t sym) {
    /*
     * Here we handle compositor keybindings. This is when the compositor is
     * processing keys, rather than passing them on to the client for its own
     * processing.
     */
    int handled = 0,
        ext = 0;
    const Key* kstart = essential_keys;
    int nk = n_essential_keys;
keyhandle:
    for (int i=0; i<nk; ++i) {
        const Key* k = kstart+i;
        if (CLEANMASK(mods) == CLEANMASK(k->mod) && sym == k->keysym && k->func) {
            k->func(&k->arg);
            handled = 1;
        }
    }
    if (!ext) {
        ext = 1;
        kstart = C->keys;
        nk = C->n_keys;
        if (kstart != NULL)
            goto keyhandle;
    }

    if (defer_reload) {
        defer_reload = 0;
        plugin_reload();
    }
    return handled;
}

void keypress(struct wl_listener *listener, void *data) {
    /* This event is raised when a key is pressed or released. */
    Keyboard *kb = wl_container_of(listener, kb, key);
    struct wlr_keyboard_key_event *event = data;

    /* Translate libinput keycode -> xkbcommon */
    uint32_t keycode = event->keycode + 8;
    /* Get a list of keysyms based on the keymap for this keyboard */
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(
            kb->wlr_keyboard->xkb_state, keycode, &syms);

    int handled = 0;
    uint32_t mods = wlr_keyboard_get_modifiers(kb->wlr_keyboard);

    IDLE_NOTIFY_ACTIVITY;

    /* On _press_ if there is no active screen locker,
     * attempt to process a compositor keybinding. */
    if (!B->locked && !B->input_inhibit_mgr->active_inhibitor
            && event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
        for (int i=0; i<nsyms; i++)
            handled = keybinding(mods, syms[i]) || handled;

    if (handled && kb->wlr_keyboard->repeat_info.delay > 0) {
        kb->mods = mods;
        kb->keysyms = syms;
        kb->nsyms = nsyms;
        wl_event_source_timer_update(kb->key_repeat_source,
                kb->wlr_keyboard->repeat_info.delay);
    } else {
        kb->nsyms = 0;
        wl_event_source_timer_update(kb->key_repeat_source, 0);
    }

    if (handled)
        return;

    /* Pass unhandled keycodes along to the client. */
    wlr_seat_set_keyboard(B->seat, kb->wlr_keyboard);
    wlr_seat_keyboard_notify_key(B->seat, event->time_msec,
        event->keycode, event->state);
}

void keypressmod(struct wl_listener *listener, void *data) {
    (void)data;
    /* This event is raised when a modifier key, such as shift or alt, is
     * pressed. We simply communicate this to the client. */
    Keyboard *kb = wl_container_of(listener, kb, modifiers);
    /*
     * A seat can only have one keyboard, but this is a limitation of the
     * Wayland protocol - not wlroots. We assign all connected keyboards to the
     * same seat. You can swap out the underlying wlr_keyboard like this and
     * wlr_seat handles this transparently.
     */
    wlr_seat_set_keyboard(B->seat, kb->wlr_keyboard);
    /* Send modifiers to the client. */
    wlr_seat_keyboard_notify_modifiers(B->seat,
        &kb->wlr_keyboard->modifiers);
}

int keyrepeat(void *data) {
    Keyboard *kb = data;
    if (!kb->nsyms || kb->wlr_keyboard->repeat_info.rate <= 0)
        return 0;

    wl_event_source_timer_update(kb->key_repeat_source,
            1000 / kb->wlr_keyboard->repeat_info.rate);

    for (int i=0; i<kb->nsyms; i++)
        keybinding(kb->mods, kb->keysyms[i]);

    return 0;
}

void killclient(const Arg *arg) {
    (void)arg;
    Client *sel = focustop(B->selmon);
    if (sel)
        client_send_close(sel);
}

void locksession(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_session_lock_v1 *session_lock = data;
    SessionLock *lock;
    wlr_scene_node_set_enabled(&B->locked_bg->node, 1);
    if (B->cur_lock) {
        wlr_session_lock_v1_destroy(session_lock);
        return;
    }
    lock = ecalloc(1, sizeof(*lock));
    focusclient(NULL, 0);

    lock->scene = wlr_scene_tree_create(B->layers[LyrBlock]);
    B->cur_lock = lock->lock = session_lock;
    B->locked = 1;
    session_lock->data = lock;

    LISTEN(&session_lock->events.new_surface, &lock->new_surface, createlocksurface);
    LISTEN(&session_lock->events.destroy, &lock->destroy, destroysessionlock);
    LISTEN(&session_lock->events.unlock, &lock->unlock, unlocksession);

    wlr_session_lock_v1_send_locked(session_lock);
}

void maplayersurfacenotify(struct wl_listener *listener, void *data) {
    (void)data;
    LayerSurface *l = wl_container_of(listener, l, map);
    motionnotify(0);
}

void mapnotify(struct wl_listener *listener, void *data) {
    (void)data;
    /* Called when the surface is mapped, or ready to display on-screen. */
    Client *p, *w, *c = wl_container_of(listener, c, map);
    Monitor *m;
    int i;

    /* Create scene tree for this client and its border */
    c->scene = wlr_scene_tree_create(B->layers[LyrTile]);
    wlr_scene_node_set_enabled(&c->scene->node, c->type != XDGShell);
    c->scene_surface = c->type == XDGShell
            ? wlr_scene_xdg_surface_create(c->scene, c->surface.xdg)
            : wlr_scene_subsurface_tree_create(c->scene, client_surface(c));
    if (client_surface(c)) {
        client_surface(c)->data = c->scene;
        /* Ideally we should do this in createnotify{,x11} but at that moment
        * wlr_xwayland_surface doesn't have wlr_surface yet. */
        LISTEN(&client_surface(c)->events.commit, &c->commit, commitnotify);
    }
    c->scene->node.data = c->scene_surface->node.data = c;

    /* Handle unmanaged clients first so we can return prior create borders */
    if (client_is_unmanaged(c)) {
        client_get_geometry(c, &c->geom);
        /* Unmanaged clients always are floating */
        wlr_scene_node_reparent(&c->scene->node, B->layers[LyrFloat]);
        wlr_scene_node_set_position(&c->scene->node, c->geom.x + C->borderpx,
            c->geom.y + C->borderpx);
        if (client_wants_focus(c)) {
            focusclient(c, 1);
            B->exclusive_focus = c;
        }
        goto unset_fullscreen;
    }

    for (i = 0; i < 4; i++) {
        c->border[i] = wlr_scene_rect_create(c->scene, 0, 0, C->bordercolor);
        c->border[i]->node.data = c;
    }

    /* Initialize client geometry with room for border */
    client_set_tiled(c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
    client_get_geometry(c, &c->geom);
    c->geom.width += 2 * c->bw;
    c->geom.height += 2 * c->bw;

    /* Insert this client into client lists. */
    wl_list_insert(&B->clients, &c->link);
    wl_list_insert(&B->fstack, &c->flink);

    /* Set initial monitor, tags, floating status, and focus:
     * we always consider floating, clients that have parent and thus
     * we set the same tags and monitor than its parent, if not
     * try to apply rules for them */
     /* TODO: https://github.com/djpohly/dwl/pull/334#issuecomment-1330166324 */
    if (c->type == XDGShell && (p = client_get_parent(c))) {
        c->isfloating = 1;
        wlr_scene_node_reparent(&c->scene->node, B->layers[LyrFloat]);
        setmon(c, p->mon, p->tags);
    } else {
        applyrules(c);
    }

    const char* appid = client_get_appid(c);
    const char* title = client_get_title(c);
    if (!appid) appid = B->broken;
    if (!title) title = B->broken;
    struct wlr_box geom = {0},
                   size_hints_max = {0},
                   size_hints_min = {0};
    client_get_geometry(c, &geom);
    client_get_size_hints(c, &size_hints_max, &size_hints_min);
    awl_log_printf( "insert client %p into list:\n"
            "title: %s\nappid: %s\nis float type: %i\nX11: %i\nwants fullscreen: %i\n"
            "wants focus: %i\nunmanaged: %i\ngeo: (%i,%i)+=(%i,%i)\n"
            "size hints: (%i,%i)+=(%i,%i) -> (%i,%i)+=(%i,%i)",
            c, title, appid, client_is_float_type(c), client_is_x11(c),
            client_wants_fullscreen(c), client_wants_focus(c), client_is_unmanaged(c),
            geom.x, geom.y, geom.width, geom.height,
            size_hints_min.x, size_hints_min.y, size_hints_min.width, size_hints_min.height,
            size_hints_max.x, size_hints_max.y, size_hints_max.width, size_hints_max.height );

    printstatus();

unset_fullscreen:
    m = c->mon ? c->mon : xytomon(c->geom.x, c->geom.y);
    wl_list_for_each(w, &B->clients, link)
        if (w != c && w->isfullscreen && m == w->mon && (w->tags & c->tags))
            setfullscreen(w, 0);
}

void maximizenotify(struct wl_listener *listener, void *data) {
    (void)data;
    /* This event is raised when a client would like to maximize itself,
     * typically because the user clicked on the maximize button on
     * client-side decorations. dwl doesn't support maximization, but
     * to conform to xdg-shell protocol we still must send a configure.
     * wlr_xdg_surface_schedule_configure() is used to send an empty reply. */
    Client *c = wl_container_of(listener, c, maximize);
    wlr_xdg_surface_schedule_configure(c->surface.xdg);
}

void monocle(Monitor *m) {
    Client *c;
    int n = 0;

    wl_list_for_each(c, &B->clients, link) {
        if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen || !c->visible)
            continue;
        resize(c, m->w, 0);
        n++;
    }
    if (n)
        snprintf(m->ltsymbol, LENGTH(m->ltsymbol), "[%d]", n);
    if ((c = focustop(m)))
        wlr_scene_node_raise_to_top(&c->scene->node);
}

void motionabsolute(struct wl_listener *listener, void *data) {
    (void)listener;
    /* This event is forwarded by the cursor when a pointer emits an _absolute_
     * motion event, from 0..1 on each axis. This happens, for example, when
     * wlroots is running under a Wayland window rather than KMS+DRM, and you
     * move the mouse over the window. You could enter the window from any edge,
     * so we have to warp the mouse there. There is also some hardware which
     * emits these events. */
    struct wlr_pointer_motion_absolute_event *event = data;
    wlr_cursor_warp_absolute(B->cursor, &event->pointer->base, event->x, event->y);
    motionnotify(event->time_msec);
}

void motionnotify(uint32_t time) {
    double sx = 0, sy = 0;
    Client *c = NULL, *w = NULL;
    LayerSurface *l = NULL;
    int type;
    struct wlr_surface *surface = NULL;

    /* time is 0 in internal calls meant to restore pointer focus. */
    if (time) {
        IDLE_NOTIFY_ACTIVITY;

        /* Update selmon (even while dragging a window) */
        if (C->sloppyfocus)
            B->selmon = xytomon(B->cursor->x, B->cursor->y);
    }

    /* Update drag icon's position */
    wlr_scene_node_set_position(&B->drag_icon->node, B->cursor->x, B->cursor->y);

    /* If we are currently grabbing the mouse, handle and return */
    if (B->cursor_mode == CurMove) {
        /* Move the grabbed client to the new position. */
        resize(B->grabc, (struct wlr_box){.x = B->cursor->x - B->grabcx, .y = B->cursor->y - B->grabcy,
            .width = B->grabc->geom.width, .height = B->grabc->geom.height}, 1);
        return;
    } else if (B->cursor_mode == CurResize) {
        resize(B->grabc, (struct wlr_box){.x = B->grabc->geom.x, .y = B->grabc->geom.y,
            .width = B->cursor->x - B->grabc->geom.x, .height = B->cursor->y - B->grabc->geom.y}, 1);
        return;
    }

    /* Find the client under the pointer and send the event along. */
    xytonode(B->cursor->x, B->cursor->y, &surface, &c, NULL, &sx, &sy);

    if (B->cursor_mode == CurPressed && !B->seat->drag) {
        if ((type = toplevel_from_wlr_surface(
                 B->seat->pointer_state.focused_surface, &w, &l)) >= 0) {
            c = w;
            surface = B->seat->pointer_state.focused_surface;
            sx = B->cursor->x - (type == LayerShell ? l->geom.x : w->geom.x);
            sy = B->cursor->y - (type == LayerShell ? l->geom.y : w->geom.y);
        }
    }

    /* If there's no client surface under the cursor, set the cursor image to a
     * default. This is what makes the cursor image appear when you move it
     * off of a client or over its border. */
    if (!surface && !B->seat->drag && strcmp(B->cursor_image, "left_ptr"))
        strcpy(B->cursor_image, "left_ptr"),
        wlr_xcursor_manager_set_cursor_image(B->cursor_mgr, B->cursor_image, B->cursor);

    pointerfocus(c, surface, sx, sy, time);
}

void motionrelative(struct wl_listener *listener, void *data) {
    (void)listener;
    /* This event is forwarded by the cursor when a pointer emits a _relative_
     * pointer motion event (i.e. a delta) */
    struct wlr_pointer_motion_event *event = data;
    /* The cursor doesn't move unless we tell it to. The cursor automatically
     * handles constraining the motion to the output layout, as well as any
     * special configuration applied for the specific input device which
     * generated the event. You can pass NULL for the device if you want to move
     * the cursor around without any input. */
    wlr_cursor_move(B->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    motionnotify(event->time_msec);
}

void moveresize(const Arg *arg) {
    if (B->cursor_mode != CurNormal && B->cursor_mode != CurPressed)
        return;
    xytonode(B->cursor->x, B->cursor->y, NULL, &B->grabc, NULL, NULL, NULL);
    if (!B->grabc || client_is_unmanaged(B->grabc) || B->grabc->isfullscreen)
        return;

    /* Float the window and tell motionnotify to grab it */
    setfloating(B->grabc, 1);
    switch (B->cursor_mode = arg->ui) {
    case CurMove:
        B->grabcx = B->cursor->x - B->grabc->geom.x;
        B->grabcy = B->cursor->y - B->grabc->geom.y;
        strcpy(B->cursor_image, "fleur");
        wlr_xcursor_manager_set_cursor_image(B->cursor_mgr, B->cursor_image, B->cursor);
        break;
    case CurResize:
        /* Doesn't work for X11 output - the next absolute motion event
         * returns the cursor to where it started */
        wlr_cursor_warp_closest(B->cursor, NULL,
                B->grabc->geom.x + B->grabc->geom.width,
                B->grabc->geom.y + B->grabc->geom.height);
        strcpy(B->cursor_image,"bottom_right_corner");
        wlr_xcursor_manager_set_cursor_image(B->cursor_mgr, B->cursor_image, B->cursor);
        break;
    }
}

void outputmgrapply(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_output_configuration_v1 *config = data;
    outputmgrapplyortest(config, 0);
}

void outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test) {
    /*
     * Called when a client such as wlr-randr requests a change in output
     * configuration. This is only one way that the layout can be changed,
     * so any Monitor information should be updated by updatemons() after an
     * output_layout.change event, not here.
     */
    struct wlr_output_configuration_head_v1 *config_head;
    int ok = 1;

    wl_list_for_each(config_head, &config->heads, link) {
        struct wlr_output *wlr_output = config_head->state.output;
        Monitor *m = wlr_output->data;

        wlr_output_enable(wlr_output, config_head->state.enabled);
        if (!config_head->state.enabled)
            goto apply_or_test;
        if (config_head->state.mode)
            wlr_output_set_mode(wlr_output, config_head->state.mode);
        else
            wlr_output_set_custom_mode(wlr_output,
                    config_head->state.custom_mode.width,
                    config_head->state.custom_mode.height,
                    config_head->state.custom_mode.refresh);

        /* Don't move monitors if position wouldn't change, this to avoid
         * wlroots marking the output as manually configured */
        if (m->m.x != config_head->state.x || m->m.y != config_head->state.y)
            wlr_output_layout_move(B->output_layout, wlr_output,
                    config_head->state.x, config_head->state.y);
        wlr_output_set_transform(wlr_output, config_head->state.transform);
        wlr_output_set_scale(wlr_output, config_head->state.scale);
        wlr_output_enable_adaptive_sync(wlr_output,
                config_head->state.adaptive_sync_enabled);

apply_or_test:
        if (test) {
            ok &= wlr_output_test(wlr_output);
            wlr_output_rollback(wlr_output);
        } else {
            ok &= wlr_output_commit(wlr_output);
        }
    }

    if (ok)
        wlr_output_configuration_v1_send_succeeded(config);
    else
        wlr_output_configuration_v1_send_failed(config);
    wlr_output_configuration_v1_destroy(config);

    /* TODO: use a wrapper function? */
    updatemons(NULL, NULL);
}

void outputmgrtest(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_output_configuration_v1 *config = data;
    outputmgrapplyortest(config, 1);
}

void pointerfocus(Client *c, struct wlr_surface *surface, double sx, double sy,
        uint32_t time) {
    struct timespec now;
    int internal_call = !time;

    if (C->sloppyfocus && !internal_call && c && !client_is_unmanaged(c))
        focusclient(c, 0);

    /* If surface is NULL, clear pointer focus */
    if (!surface) {
        wlr_seat_pointer_notify_clear_focus(B->seat);
        return;
    }

    if (internal_call) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        time = now.tv_sec * 1000 + now.tv_nsec / 1000000;
    }

    /* Let the client know that the mouse cursor has entered one
     * of its surfaces, and make keyboard focus follow if desired.
     * wlroots makes this a no-op if surface is already focused */
    wlr_seat_pointer_notify_enter(B->seat, surface, sx, sy);
    wlr_seat_pointer_notify_motion(B->seat, time, sx, sy);
}

void printstatus(void) {
    Monitor *m = NULL;

    wl_list_for_each(m, &B->mons, link)
        dwl_ipc_output_printstatus(m);
}

void quit(const Arg *arg) {
    (void)arg;
    wl_display_terminate(B->dpy);
}

void rendermon(struct wl_listener *listener, void *data) {
    (void)data;
    /* This function is called every time an output is ready to display a frame,
     * generally at the output's refresh rate (e.g. 60Hz). */
    Monitor *m = wl_container_of(listener, m, frame);
    Client *c;
    struct timespec now;

    /* Render if no XDG clients have an outstanding resize and are visible on
     * this monitor. */
    wl_list_for_each(c, &B->clients, link)
        if (c->resize && !c->isfloating && client_is_rendered_on_mon(c, m) && !client_is_stopped(c))
            goto skip;
    wlr_scene_output_commit(m->scene_output);

skip:
    /* Let clients know a frame has been rendered */
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(m->scene_output, &now);
}

void requeststartdrag(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_seat_request_start_drag_event *event = data;

    if (wlr_seat_validate_pointer_grab_serial(B->seat, event->origin,
            event->serial))
        wlr_seat_start_pointer_drag(B->seat, event->drag, event->serial);
    else
        wlr_data_source_destroy(event->drag->source);
}

void
resize(Client *c, struct wlr_box geo, int interact)
{
    struct wlr_box *bbox = interact ? &B->sgeom : &c->mon->w;
    client_set_bounds(c, geo.width, geo.height);
    c->geom = geo;
    applybounds(c, bbox);

    /* Update scene-graph, including borders */
    wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
    wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);
    wlr_scene_rect_set_size(c->border[0], c->geom.width, c->bw);
    wlr_scene_rect_set_size(c->border[1], c->geom.width, c->bw);
    wlr_scene_rect_set_size(c->border[2], c->bw, c->geom.height - 2 * c->bw);
    wlr_scene_rect_set_size(c->border[3], c->bw, c->geom.height - 2 * c->bw);
    wlr_scene_node_set_position(&c->border[1]->node, 0, c->geom.height - c->bw);
    wlr_scene_node_set_position(&c->border[2]->node, 0, c->bw);
    wlr_scene_node_set_position(&c->border[3]->node, c->geom.width - c->bw, c->bw);

    /* this is a no-op if size hasn't changed */
    c->resize = client_set_size(c, c->geom.width - 2 * c->bw,
            c->geom.height - 2 * c->bw);
}

void run(char *startup_cmd) {
    awl_log_printf( "awl main loop" );
    /* Add a Unix socket to the Wayland display. */
    const char *socket = wl_display_add_socket_auto(B->dpy);
    if (!socket)
        die("startup: display_add_socket_auto");
    setenv("WAYLAND_DISPLAY", socket, 1);

    /* Start the backend. This will enumerate outputs and inputs, become the DRM
     * master, etc */
    if (!wlr_backend_start(B->backend))
        die("startup: backend_start");

    /* Now that the socket exists and the backend is started, run the startup command */
    if (startup_cmd) {
        int piperw[2];
        if (pipe(piperw) < 0)
            die("startup: pipe:");
        if ((B->child_pid = fork()) < 0)
            die("startup: fork:");
        if (B->child_pid == 0) {
            dup2(piperw[0], STDIN_FILENO);
            close(piperw[0]);
            close(piperw[1]);
            execl("/bin/sh", "/bin/sh", "-c", startup_cmd, NULL);
            die("startup: execl:");
        }
        dup2(piperw[1], STDOUT_FILENO);
        close(piperw[1]);
        close(piperw[0]);
    }
    printstatus();

    /* At this point the outputs are initialized, choose initial selmon based on
     * cursor position, and set default cursor image */
    B->selmon = xytomon(B->cursor->x, B->cursor->y);

    /* TODO hack to get cursor to display in its initial location (100, 100)
     * instead of (0, 0) and then jumping. still may not be fully
     * initialized, as the image/coordinates are not transformed for the
     * monitor when displayed here */
    wlr_cursor_warp_closest(B->cursor, NULL, B->cursor->x, B->cursor->y);
    wlr_xcursor_manager_set_cursor_image(B->cursor_mgr, B->cursor_image, B->cursor);

    /* Run the Wayland event loop. This does not return until you exit the
     * compositor. Starting the backend rigged up all of the necessary event
     * loop configuration to listen to libinput events, DRM events, generate
     * frame events at the refresh rate, and so on. */
    awl_ready = 1;
    wl_display_run(B->dpy);
}

void setcursor(struct wl_listener *listener, void *data) {
    (void)listener;
    /* This event is raised by the seat when a client provides a cursor image */
    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    /* If we're "grabbing" the cursor, don't use the client's image, we will
     * restore it after "grabbing" sending a leave event, followed by a enter
     * event, which will result in the client requesting set the cursor surface */
    if (B->cursor_mode != CurNormal && B->cursor_mode != CurPressed)
        return;
    strcpy(B->cursor_image,"");
    /* This can be sent by any client, so we check to make sure this one is
     * actually has pointer focus first. If so, we can tell the cursor to
     * use the provided surface as the cursor image. It will set the
     * hardware cursor on the output that it's currently on and continue to
     * do so as the cursor moves between outputs. */
    if (event->seat_client == B->seat->pointer_state.focused_client)
        wlr_cursor_set_surface(B->cursor, event->surface,
                event->hotspot_x, event->hotspot_y);
}

void setfloating(Client *c, int floating) {
    c->isfloating = floating;
    if (!c->mon)
        return;
    wlr_scene_node_reparent(&c->scene->node, B->layers[c->isfullscreen
            ? LyrFS : c->isfloating ? LyrFloat : LyrTile]);
    arrange(c->mon);
    printstatus();
}

void setfullscreen(Client *c, int fullscreen) {
    c->isfullscreen = fullscreen;
    if (!c->mon)
        return;
    c->bw = fullscreen ? 0 : C->borderpx;
    client_set_fullscreen(c, fullscreen);
    wlr_scene_node_reparent(&c->scene->node, B->layers[c->isfullscreen
            ? LyrFS : c->isfloating ? LyrFloat : LyrTile]);

    if (fullscreen) {
        c->prev = c->geom;
        resize(c, c->mon->m, 0);
    } else {
        /* restore previous size instead of arrange for floating windows since
         * client positions are set by the user and cannot be recalculated */
        resize(c, c->prev, 0);
    }
    arrange(c->mon);
    printstatus();
}

void setlayout(const Arg *arg) {
    if (!B->selmon || !arg)
        return;
    if (arg->i < C->n_layouts && arg->i >= 0) {
        B->selmon->sellt ^= 1;
        B->selmon->lt[B->selmon->sellt] = arg->i;
    }
    strncpy(B->selmon->ltsymbol, C->layouts[B->selmon->lt[B->selmon->sellt]].symbol,
            LENGTH(B->selmon->ltsymbol)-1);
    arrange(B->selmon);
    printstatus();
}

/* arg > 1.0 will set mfact absolutely */
void setmfact(const Arg *arg) {
    float f;

    if (!arg || !B->selmon || !C->layouts[B->selmon->lt[B->selmon->sellt]].arrange)
        return;
    f = arg->f < 1.0 ? arg->f + B->selmon->mfact : arg->f - 1.0;
    if (f < 0.1 || f > 0.9)
        return;
    B->selmon->mfact = f;
    arrange(B->selmon);
}

void setmon(Client *c, Monitor *m, uint32_t newtags) {
    Monitor *oldmon = c->mon;

    if (oldmon == m)
        return;
    c->mon = m;
    c->prev = c->geom;

    /* Scene graph sends surface leave/enter events on move and resize */
    if (oldmon)
        arrange(oldmon);
    if (m) {
        /* Make sure window actually overlaps with the monitor */
        resize(c, c->geom, 0);
        c->tags = newtags ? newtags : m->tagset[m->seltags]; /* assign tags of target monitor */
        setfullscreen(c, c->isfullscreen); /* This will call arrange(c->mon) */
        setfloating(c, c->isfloating);
    }
    focusclient(focustop(B->selmon), 1);
}

void setpsel(struct wl_listener *listener, void *data) {
    (void)listener;
    /* This event is raised by the seat when a client wants to set the selection,
     * usually when the user copies something. wlroots allows compositors to
     * ignore such requests if they so choose, but in dwl we always honor
     */
    struct wlr_seat_request_set_primary_selection_event *event = data;
    wlr_seat_set_primary_selection(B->seat, event->source, event->serial);
}

void setsel(struct wl_listener *listener, void *data) {
    (void)listener;
    /* This event is raised by the seat when a client wants to set the selection,
     * usually when the user copies something. wlroots allows compositors to
     * ignore such requests if they so choose, but in dwl we always honor
     */
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(B->seat, event->source, event->serial);
}

void setup(void) {
    awl_log_printf( "main awl setup" );
    int sig[] = {SIGCHLD, SIGINT, SIGTERM, SIGPIPE};
    struct sigaction sa = {.sa_flags = SA_RESTART, .sa_handler = handlesig};
    sigemptyset(&sa.sa_mask);

    for (unsigned i=0; i<LENGTH(sig); i++)
        sigaction(sig[i], &sa, NULL);

    wlr_log_init(log_level, NULL);

    /* The Wayland display is managed by libwayland. It handles accepting
     * clients from the Unix socket, manging Wayland globals, and so on. */
    B->dpy = wl_display_create();

    /* The backend is a wlroots feature which abstracts the underlying input and
     * output hardware. The autocreate option will choose the most suitable
     * backend based on the current environment, such as opening an X11 window
     * if an X11 server is running. The NULL argument here optionally allows you
     * to pass in a custom renderer if wlr_renderer doesn't meet your needs. The
     * backend uses the renderer, for example, to fall back to software cursors
     * if the backend does not support hardware cursors (some older GPUs
     * don't). */
    if (!(B->backend = wlr_backend_autocreate(B->dpy)))
        die("couldn't create backend");

    /* Initialize the scene graph used to lay out windows */
    B->scene = wlr_scene_create();
    for (unsigned i=0; i<NUM_LAYERS; i++)
        B->layers[i] = wlr_scene_tree_create(&B->scene->tree);
    B->drag_icon = wlr_scene_tree_create(&B->scene->tree);
    wlr_scene_node_place_below(&B->drag_icon->node, &B->layers[LyrBlock]->node);

    /* Create a renderer with the default implementation */
    if (!(B->drw = wlr_renderer_autocreate(B->backend)))
        die("couldn't create renderer");
    wlr_renderer_init_wl_display(B->drw, B->dpy);

    /* Create a default allocator */
    if (!(B->alloc = wlr_allocator_autocreate(B->backend, B->drw)))
        die("couldn't create allocator");

    /* This creates some hands-off wlroots interfaces. The compositor is
     * necessary for clients to allocate surfaces and the data device manager
     * handles the clipboard. Each of these wlroots interfaces has room for you
     * to dig your fingers in and play with their behavior if you want. Note that
     * the clients cannot set the selection directly without compositor approval,
     * see the setsel() function. */
    B->compositor = wlr_compositor_create(B->dpy, B->drw);
    wlr_export_dmabuf_manager_v1_create(B->dpy);
    wlr_screencopy_manager_v1_create(B->dpy);
    wlr_data_control_manager_v1_create(B->dpy);
    wlr_data_device_manager_create(B->dpy);
    wlr_gamma_control_manager_v1_create(B->dpy);
    wlr_primary_selection_v1_device_manager_create(B->dpy);
    wlr_viewporter_create(B->dpy);
    wlr_single_pixel_buffer_manager_v1_create(B->dpy);
    wlr_subcompositor_create(B->dpy);

    /* Initializes the interface used to implement urgency hints */
    B->activation = wlr_xdg_activation_v1_create(B->dpy);
    wl_signal_add(&B->activation->events.request_activate, &request_activate);

    /* Creates an output layout, which a wlroots utility for working with an
     * arrangement of screens in a physical layout. */
    B->output_layout = wlr_output_layout_create();
    wl_signal_add(&B->output_layout->events.change, &layout_change);
    wlr_xdg_output_manager_v1_create(B->dpy, B->output_layout);

    /* Configure a listener to be notified when new outputs are available on the
     * backend. */
    wl_list_init(&B->mons);
    wl_signal_add(&B->backend->events.new_output, &new_output);

    /* Set up our client lists and the xdg-shell. The xdg-shell is a
     * Wayland protocol which is used for application windows. For more
     * detail on shells, refer to the article:
     *
     * https://drewdevault.com/2018/07/29/Wayland-shells.html
     */
    wl_list_init(&B->clients);
    wl_list_init(&B->fstack);

    B->idle = wlr_idle_create(B->dpy);
    B->idle_notifier = wlr_idle_notifier_v1_create(B->dpy);

    B->idle_inhibit_mgr = wlr_idle_inhibit_v1_create(B->dpy);
    wl_signal_add(&B->idle_inhibit_mgr->events.new_inhibitor, &idle_inhibitor_create);

    B->layer_shell = wlr_layer_shell_v1_create(B->dpy);
    wl_signal_add(&B->layer_shell->events.new_surface, &new_layer_shell_surface);

    B->xdg_shell = wlr_xdg_shell_create(B->dpy, 4);
    wl_signal_add(&B->xdg_shell->events.new_surface, &new_xdg_surface);

    B->input_inhibit_mgr = wlr_input_inhibit_manager_create(B->dpy);
    B->session_lock_mgr = wlr_session_lock_manager_v1_create(B->dpy);
    wl_signal_add(&B->session_lock_mgr->events.new_lock, &session_lock_create_lock);
    wl_signal_add(&B->session_lock_mgr->events.destroy, &session_lock_mgr_destroy);
    B->locked_bg = wlr_scene_rect_create(B->layers[LyrBlock], B->sgeom.width, B->sgeom.height,
            (float [4]){0.1, 0.1, 0.1, 1.0});
    wlr_scene_node_set_enabled(&B->locked_bg->node, 0);

    /* Use decoration protocols to negotiate server-side decorations */
    wlr_server_decoration_manager_set_default_mode(
            wlr_server_decoration_manager_create(B->dpy),
            WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
    B->xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(B->dpy);
    wl_signal_add(&B->xdg_decoration_mgr->events.new_toplevel_decoration, &new_xdg_decoration);

    /*
     * Creates a cursor, which is a wlroots utility for tracking the cursor
     * image shown on screen.
     */
    B->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(B->cursor, B->output_layout);

    /* Creates an xcursor manager, another wlroots utility which loads up
     * Xcursor themes to source cursor images from and makes sure that cursor
     * images are available at all scale factors on the screen (necessary for
     * HiDPI support). Scaled cursors will be loaded with each output. */
    B->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    setenv("XCURSOR_SIZE", "24", 1);

    /*
     * wlr_cursor *only* displays an image on screen. It does not move around
     * when the pointer moves. However, we can attach input devices to it, and
     * it will generate aggregate events for all of them. In these events, we
     * can choose how we want to process them, forwarding them to clients and
     * moving the cursor around. More detail on this process is described in my
     * input handling blog post:
     *
     * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html
     *
     * And more comments are sprinkled throughout the notify functions above.
     */
    wl_signal_add(&B->cursor->events.motion, &cursor_motion);
    wl_signal_add(&B->cursor->events.motion_absolute, &cursor_motion_absolute);
    wl_signal_add(&B->cursor->events.button, &cursor_button);
    wl_signal_add(&B->cursor->events.axis, &cursor_axis);
    wl_signal_add(&B->cursor->events.frame, &cursor_frame);

    /*
     * Configures a seat, which is a single "seat" at which a user sits and
     * operates the computer. This conceptually includes up to one keyboard,
     * pointer, touch, and drawing tablet device. We also rig up a listener to
     * let us know when new input devices are available on the backend.
     */
    wl_list_init(&B->keyboards);
    wl_signal_add(&B->backend->events.new_input, &new_input);
    B->virtual_keyboard_mgr = wlr_virtual_keyboard_manager_v1_create(B->dpy);
    wl_signal_add(&B->virtual_keyboard_mgr->events.new_virtual_keyboard,
            &new_virtual_keyboard);
    B->seat = wlr_seat_create(B->dpy, "seat0");
    wl_signal_add(&B->seat->events.request_set_cursor, &request_cursor);
    wl_signal_add(&B->seat->events.request_set_selection, &request_set_sel);
    wl_signal_add(&B->seat->events.request_set_primary_selection, &request_set_psel);
    wl_signal_add(&B->seat->events.request_start_drag, &request_start_drag);
    wl_signal_add(&B->seat->events.start_drag, &start_drag);

    B->output_mgr = wlr_output_manager_v1_create(B->dpy);
    wl_signal_add(&B->output_mgr->events.apply, &output_mgr_apply);
    wl_signal_add(&B->output_mgr->events.test, &output_mgr_test);

    wlr_scene_set_presentation(B->scene, wlr_presentation_create(B->dpy, B->backend));
    wl_global_create(B->dpy, &zdwl_ipc_manager_v2_interface, 2, NULL, dwl_ipc_manager_bind);

    /*
     * Initialise the XWayland X server.
     * It will be started when the first X client is started.
     */
    xwayland = wlr_xwayland_create(B->dpy, B->compositor, 1);
    if (xwayland) {
        wl_signal_add(&xwayland->events.ready, &xwayland_ready);
        wl_signal_add(&xwayland->events.new_surface, &new_xwayland_surface);

        setenv("DISPLAY", xwayland->display_name, 1);
    } else {
        awl_err_printf( "failed to setup XWayland X server, continuing without it." );
    }
}

void spawn(const Arg *arg) {
    if (fork() == 0) {
        dup2(STDERR_FILENO, STDOUT_FILENO);
        setsid();
        execvp(((char **)arg->v)[0], (char **)arg->v);
        die("dwl: execvp %s failed:", ((char **)arg->v)[0]);
    }
}

void startdrag(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_drag *drag = data;
    if (!drag->icon)
        return;

    drag->icon->data = &wlr_scene_subsurface_tree_create(B->drag_icon, drag->icon->surface)->node;
    wl_signal_add(&drag->icon->events.destroy, &drag_icon_destroy);
}

void tag(const Arg *arg) {
    Client *sel = focustop(B->selmon);
    if (!sel || (arg->ui & TAGMASK) == 0)
        return;

    sel->tags = arg->ui & TAGMASK;
    focusclient(focustop(B->selmon), 1);
    arrange(B->selmon);
    printstatus();
}

void tagmon(const Arg *arg) {
    Client *sel = focustop(B->selmon);
    if (sel)
        setmon(sel, dirtomon(arg->i), 0);
}

void tile(Monitor *m) {
    unsigned int i, n = 0, mw, my, ty;
    Client *c;

    wl_list_for_each(c, &B->clients, link)
        if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen && c->visible)
            n++;
    if (n == 0)
        return;

    if (n > (unsigned)m->nmaster)
        mw = m->nmaster ? m->w.width * m->mfact : 0;
    else
        mw = m->w.width;
    i = my = ty = 0;
    wl_list_for_each(c, &B->clients, link) {
        if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen || !c->visible)
            continue;
        if (i < (unsigned)m->nmaster) {
            resize(c, (struct wlr_box){.x = m->w.x, .y = m->w.y + my, .width = mw,
                .height = (m->w.height - my) / (MIN(n, m->nmaster) - i)}, 0);
            my += c->geom.height;
        } else {
            resize(c, (struct wlr_box){.x = m->w.x + mw, .y = m->w.y + ty,
                .width = m->w.width - mw, .height = (m->w.height - ty) / (n - i)}, 0);
            ty += c->geom.height;
        }
        i++;
    }
}

void togglebar(const Arg *arg) {
    (void)arg;
    DwlIpcOutput *ipc_output;
    wl_list_for_each(ipc_output, &B->selmon->dwl_ipc_outputs, link)
        zdwl_ipc_output_v2_send_toggle_visibility(ipc_output->resource);
}

void togglefloating(const Arg *arg) {
    (void)arg;
    Client *sel = focustop(B->selmon);
    /* return if fullscreen */
    if (sel && !sel->isfullscreen)
        setfloating(sel, !sel->isfloating);
}

void togglefullscreen(const Arg *arg) {
    (void)arg;
    Client *sel = focustop(B->selmon);
    if (sel)
        setfullscreen(sel, !sel->isfullscreen);
}

void toggletag(const Arg *arg) {
    uint32_t newtags;
    Client *sel = focustop(B->selmon);
    if (!sel)
        return;
    newtags = sel->tags ^ (arg->ui & TAGMASK);
    if (!newtags)
        return;

    sel->tags = newtags;
    focusclient(focustop(B->selmon), 1);
    arrange(B->selmon);
    printstatus();
}

void toggleview(const Arg *arg) {
    uint32_t newtagset = B->selmon ? B->selmon->tagset[B->selmon->seltags] ^ (arg->ui & TAGMASK) : 0;

    if (!newtagset)
        return;

    B->selmon->tagset[B->selmon->seltags] = newtagset;
    focusclient(focustop(B->selmon), 1);
    arrange(B->selmon);
    printstatus();
}

void unlocksession(struct wl_listener *listener, void *data) {
    (void)data;
    SessionLock *lock = wl_container_of(listener, lock, unlock);
    destroylock(lock, 1);
}

void unmaplayersurfacenotify(struct wl_listener *listener, void *data) {
    (void)data;
    LayerSurface *layersurface = wl_container_of(listener, layersurface, unmap);

    layersurface->mapped = 0;
    wlr_scene_node_set_enabled(&layersurface->scene->node, 0);
    if (layersurface == B->exclusive_focus)
        B->exclusive_focus = NULL;
    if (layersurface->layer_surface->output
            && (layersurface->mon = layersurface->layer_surface->output->data))
        arrangelayers(layersurface->mon);
    if (layersurface->layer_surface->surface ==
            B->seat->keyboard_state.focused_surface)
        focusclient(focustop(B->selmon), 1);
    motionnotify(0);
}

void unmapnotify(struct wl_listener *listener, void *data) {
    (void)data;
    /* Called when the surface is unmapped, and should no longer be shown. */
    Client *c = wl_container_of(listener, c, unmap);
    if (c == B->grabc) {
        B->cursor_mode = CurNormal;
        B->grabc = NULL;
    }

    if (client_is_unmanaged(c)) {
        if (c == B->exclusive_focus)
            B->exclusive_focus = NULL;
        if (client_surface(c) == B->seat->keyboard_state.focused_surface)
            focusclient(focustop(B->selmon), 1);
    } else {
        wl_list_remove(&c->link);
        setmon(c, NULL, 0);
        wl_list_remove(&c->flink);
    }

    wl_list_remove(&c->commit.link);
    wlr_scene_node_destroy(&c->scene->node);
    printstatus();
    motionnotify(0);
}

void updatemons(struct wl_listener *listener, void *data) {
    (void)listener;
    (void)data;
    /*
     * Called whenever the output layout changes: adding or removing a
     * monitor, changing an output's mode or position, etc. This is where
     * the change officially happens and we update geometry, window
     * positions, focus, and the stored configuration in wlroots'
     * output-manager implementation.
     */
    struct wlr_output_configuration_v1 *config =
        wlr_output_configuration_v1_create();
    Client *c;
    struct wlr_output_configuration_head_v1 *config_head;
    Monitor *m;

    /* First remove from the layout the disabled monitors */
    wl_list_for_each(m, &B->mons, link) {
        if (m->wlr_output->enabled)
            continue;
        config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);
        config_head->state.enabled = 0;
        /* Remove this output from the layout to avoid cursor enter inside it */
        wlr_output_layout_remove(B->output_layout, m->wlr_output);
        closemon(m);
        memset(&m->m, 0, sizeof(m->m));
        memset(&m->w, 0, sizeof(m->w));
    }
    /* Insert outputs that need to */
    wl_list_for_each(m, &B->mons, link)
        if (m->wlr_output->enabled
                && !wlr_output_layout_get(B->output_layout, m->wlr_output))
            wlr_output_layout_add_auto(B->output_layout, m->wlr_output);

    /* Now that we update the output layout we can get its box */
    wlr_output_layout_get_box(B->output_layout, NULL, &B->sgeom);

    /* Make sure the clients are hidden when dwl is locked */
    wlr_scene_node_set_position(&B->locked_bg->node, B->sgeom.x, B->sgeom.y);
    wlr_scene_rect_set_size(B->locked_bg, B->sgeom.width, B->sgeom.height);

    wl_list_for_each(m, &B->mons, link) {
        if (!m->wlr_output->enabled)
            continue;
        config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);

        /* Get the effective monitor geometry to use for surfaces */
        wlr_output_layout_get_box(B->output_layout, m->wlr_output, &(m->m));
        wlr_output_layout_get_box(B->output_layout, m->wlr_output, &(m->w));
        wlr_scene_output_set_position(m->scene_output, m->m.x, m->m.y);

        wlr_scene_node_set_position(&m->fullscreen_bg->node, m->m.x, m->m.y);
        wlr_scene_rect_set_size(m->fullscreen_bg, m->m.width, m->m.height);

        if (m->lock_surface) {
            struct wlr_scene_tree *scene_tree = m->lock_surface->surface->data;
            wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
            wlr_session_lock_surface_v1_configure(m->lock_surface, m->m.width,
                    m->m.height);
        }

        /* Calculate the effective monitor geometry to use for clients */
        arrangelayers(m);
        /* Don't move clients to the left output when plugging monitors */
        arrange(m);

        config_head->state.enabled = 1;
        config_head->state.mode = m->wlr_output->current_mode;
        config_head->state.x = m->m.x;
        config_head->state.y = m->m.y;
    }

    if (B->selmon && B->selmon->wlr_output->enabled) {
        wl_list_for_each(c, &B->clients, link)
            if (!c->mon && client_is_mapped(c))
                setmon(c, B->selmon, c->tags);
        focusclient(focustop(B->selmon), 1);
        if (B->selmon->lock_surface) {
            client_notify_enter(B, B->selmon->lock_surface->surface,
                    wlr_seat_get_keyboard(B->seat));
            client_activate_surface(B->selmon->lock_surface->surface, 1);
        }
    }

    wlr_output_manager_v1_set_configuration(B->output_mgr, config);
}

void updatetitle(struct wl_listener *listener, void *data) {
    (void)data;
    Client *c = wl_container_of(listener, c, set_title);
    if (c == focustop(c->mon))
        printstatus();
}

void urgent(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_xdg_activation_v1_request_activate_event *event = data;
    Client *c = NULL;
    toplevel_from_wlr_surface(event->surface, &c, NULL);
    if (!c || c == focustop(B->selmon))
        return;

    if (client_is_mapped(c))
        client_set_border_color(c, C->urgentcolor);
    c->isurgent = 1;
    printstatus();
}

void view(const Arg *arg) {
    if (!B->selmon || (arg->ui & TAGMASK) == B->selmon->tagset[B->selmon->seltags])
        return;
    B->selmon->seltags ^= 1; /* toggle sel tagset */
    if (arg->ui & TAGMASK)
        B->selmon->tagset[B->selmon->seltags] = arg->ui & TAGMASK;
    focusclient(focustop(B->selmon), 1);
    arrange(B->selmon);
    printstatus();
}

void virtualkeyboard(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_virtual_keyboard_v1 *keyboard = data;
    createkeyboard(&keyboard->keyboard);
}

Monitor * xytomon(double x, double y) {
    struct wlr_output *o = wlr_output_layout_output_at(B->output_layout, x, y);
    return o ? o->data : NULL;
}

void xytonode(double x, double y, struct wlr_surface **psurface, Client **pc,
        LayerSurface **pl, double *nx, double *ny) {
    struct wlr_scene_node *node, *pnode;
    struct wlr_surface *surface = NULL;
    Client *c = NULL;
    LayerSurface *l = NULL;
    int layer;

    for (layer = NUM_LAYERS - 1; !surface && layer >= 0; layer--) {
        if (!(node = wlr_scene_node_at(&B->layers[layer]->node, x, y, nx, ny)))
            continue;

        if (node->type == WLR_SCENE_NODE_BUFFER)
            surface = wlr_scene_surface_from_buffer(
                    wlr_scene_buffer_from_node(node))->surface;
        /* Walk the tree to find a node that knows the client */
        for (pnode = node; pnode && !c; pnode = &pnode->parent->node)
            c = pnode->data;
        if (c && c->type == LayerShell) {
            c = NULL;
            l = pnode->data;
        }
    }

    if (psurface) *psurface = surface;
    if (pc) *pc = c;
    if (pl) *pl = l;
}

void zoom(const Arg *arg) {
    (void)arg;
    Client *c, *sel = focustop(B->selmon);

    if (!sel || !B->selmon || !C->layouts[B->selmon->lt[B->selmon->sellt]].arrange || sel->isfloating)
        return;

    /* Search for the first tiled window that is not sel, marking sel as
     * NULL if we pass it along the way */
    wl_list_for_each(c, &B->clients, link)
        if (VISIBLEON(c, B->selmon) && !c->isfloating) {
            if (c != sel)
                break;
            sel = NULL;
        }

    /* Return if no other tiled window was found */
    if (&c->link == &B->clients)
        return;

    /* If we passed sel, move c to the front; otherwise, move sel to the
     * front */
    if (!sel)
        sel = c;
    wl_list_remove(&sel->link);
    wl_list_insert(&B->clients, &sel->link);

    focusclient(sel, 1);
    arrange(B->selmon);
}

void activatex11(struct wl_listener *listener, void *data) {
    (void)data;
    Client *c = wl_container_of(listener, c, activate);

    /* Only "managed" windows can be activated */
    if (c->type == X11Managed)
        wlr_xwayland_surface_activate(c->surface.xwayland, 1);
}

void configurex11(struct wl_listener *listener, void *data) {
    Client *c = wl_container_of(listener, c, configure);
    struct wlr_xwayland_surface_configure_event *event = data;
    if (!c->mon)
        return;
    if (c->isfloating || c->type == X11Unmanaged)
        resize(c, (struct wlr_box){.x = event->x, .y = event->y,
                .width = event->width, .height = event->height}, 0);
    else
        arrange(c->mon);
}

void createnotifyx11(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_xwayland_surface *xsurface = data;
    Client *c;

    /* Allocate a Client for this surface */
    c = xsurface->data = ecalloc(1, sizeof(*c));
    c->surface.xwayland = xsurface;
    c->type = xsurface->override_redirect ? X11Unmanaged : X11Managed;
    c->bw = C->borderpx;
    c->visible = 1;

    /* Listen to the various events it can emit */
    LISTEN(&xsurface->events.map, &c->map, mapnotify);
    LISTEN(&xsurface->events.unmap, &c->unmap, unmapnotify);
    LISTEN(&xsurface->events.request_activate, &c->activate, activatex11);
    LISTEN(&xsurface->events.request_configure, &c->configure, configurex11);
    LISTEN(&xsurface->events.set_hints, &c->set_hints, sethints);
    LISTEN(&xsurface->events.set_title, &c->set_title, updatetitle);
    LISTEN(&xsurface->events.destroy, &c->destroy, destroynotify);
    LISTEN(&xsurface->events.request_fullscreen, &c->fullscreen, fullscreennotify);
}

xcb_atom_t getatom(xcb_connection_t *xc, const char *name) {
    xcb_atom_t atom = 0;
    xcb_intern_atom_reply_t *reply;
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(xc, 0, strlen(name), name);
    if ((reply = xcb_intern_atom_reply(xc, cookie, NULL)))
        atom = reply->atom;
    free(reply);

    return atom;
}

void sethints(struct wl_listener *listener, void *data) {
    (void)data;
    Client *c = wl_container_of(listener, c, set_hints);
    if (c == focustop(B->selmon))
        return;

    c->isurgent = xcb_icccm_wm_hints_get_urgency(c->surface.xwayland->hints);

    if (c->isurgent && client_is_mapped(c))
        client_set_border_color(c, C->urgentcolor);

    printstatus();
}

void xwaylandready(struct wl_listener *listener, void *data) {
    (void)data;
    (void)listener;
    struct wlr_xcursor *xcursor;
    xcb_connection_t *xc = xcb_connect(xwayland->display_name, NULL);
    int err = xcb_connection_has_error(xc);
    if (err) {
        awl_err_printf( "xcb_connect to X server failed (code %d) -> degraded functionality", err);
        return;
    }

    /* Collect atoms we are interested in. If getatom returns 0, we will
     * not detect that window type. */
    netatom[NetWMWindowTypeDialog] = getatom(xc, "_NET_WM_WINDOW_TYPE_DIALOG");
    netatom[NetWMWindowTypeSplash] = getatom(xc, "_NET_WM_WINDOW_TYPE_SPLASH");
    netatom[NetWMWindowTypeToolbar] = getatom(xc, "_NET_WM_WINDOW_TYPE_TOOLBAR");
    netatom[NetWMWindowTypeUtility] = getatom(xc, "_NET_WM_WINDOW_TYPE_UTILITY");

    /* assign the one and only seat */
    wlr_xwayland_set_seat(xwayland, B->seat);

    /* Set the default XWayland cursor to match the rest of dwl. */
    if ((xcursor = wlr_xcursor_manager_get_xcursor(B->cursor_mgr, "left_ptr", 1)))
        wlr_xwayland_set_cursor(xwayland,
                xcursor->images[0]->buffer, xcursor->images[0]->width * 4,
                xcursor->images[0]->width, xcursor->images[0]->height,
                xcursor->images[0]->hotspot_x, xcursor->images[0]->hotspot_y);

    xcb_disconnect(xc);
}

static void defer_reload_fun(const Arg* arg) {
    (void)arg;
    defer_reload = 1;
}

static void plugin_reload(void) {
    V->free();
    awl_extension_refresh(E);
    V = awl_extension_vtable(E);
    V->state = B;
    V->init();
    C = V->config;
}

static void plugin_init(const char* lib) {
    awl_log_printf( "init plugin architecture" );
    E = awl_extension_init(lib);
    V = awl_extension_vtable(E);
    V->state = B;
    V->init();
    C = V->config;
}

static void plugin_free(void) {
    awl_log_printf( "free plugin architecture" );
    V->free();
    awl_extension_free(E);
}

int main(int argc, char *argv[]) {
    char *startup_cmd = NULL;
    int c;
    int awl_loglevel = 2;

    while ((c = getopt(argc, argv, "s:hdl:v")) != -1) {
        switch (c) {
            case 's': startup_cmd = optarg; break;
            case 'd': log_level = WLR_DEBUG; awl_loglevel = 5; break;
            case 'v': die("awl " VERSION); break;
            case 'l': awl_loglevel = atoi(optarg); break;
            default:
            usage:
                die("Usage: %s [-v] [-d] [-s startup command] [-l loglevel]", argv[0]);
                break;
        }
    }
    if (optind < argc)
        goto usage;

    awl_log_init( awl_loglevel );
    atexit( awl_log_destroy );
    B = awl_state_init();

    /* Wayland requires XDG_RUNTIME_DIR for creating its communications socket */
    if (!getenv("XDG_RUNTIME_DIR"))
        die("XDG_RUNTIME_DIR must be set");
    setup();

    plugin_init(NULL);

    run(startup_cmd);

    cleanup();
    plugin_free();

    awl_state_free(B);
    return EXIT_SUCCESS;

}

