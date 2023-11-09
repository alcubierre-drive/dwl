#define _GNU_SOURCE
#include "minimal.h"

#include <linux/input-event-codes.h>

#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-util.h>

#include <pthread.h>

#include "xdg-shell-protocol.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"

struct PointerEvent {
    uint32_t event_mask;
    wl_fixed_t surface_x, surface_y;
    uint32_t button, state;
    uint32_t time;
    uint32_t serial;
    struct {
            int valid;
            wl_fixed_t value;
            int32_t discrete;
    } axes[2];
    uint32_t axis_source;
};

typedef struct Window Window;
typedef struct {
    struct wl_output *wl_output;
    struct wl_surface *wl_surface;
    struct zwlr_layer_surface_v1 *layer_surface;

    uint32_t registry_name;

    int configured;
    uint32_t width, height;
    uint32_t stride, bufsize;

    int hidden;
    int redraw;

    Window* parent;

    struct wl_list link;
} SingleWindow;

typedef struct {
    struct wl_seat *wl_seat;
    struct wl_pointer *wl_pointer;
    uint32_t registry_name;

    SingleWindow *win;
    uint32_t pointer_x, pointer_y;
    uint32_t pointer_button;
    double continuous_event;
    double continuous_event_norm;
    struct PointerEvent pointer_event;

    Window* parent;

    struct wl_list link;
} WaylandSeat;

struct Window {
    int has_init;
    int redraw_fd;

    int only_current_output;
    int hidden;
    int running;

    uint32_t buffer_scale;

    struct wl_registry *registry;
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;

    struct wl_cursor_image *cursor_image;
    struct wl_surface *cursor_surface;

    struct wl_list window_list;
    struct wl_list seat_list;

    struct zwlr_layer_surface_v1_listener layer_surface_listener;
    struct wl_pointer_listener pointer_listener;
    struct wl_seat_listener seat_listener;
    struct wl_registry_listener registry_listener;

    void (*draw)( SingleWindow* win, pixman_image_t* foreground, pixman_image_t* background );
    void (*click)( SingleWindow* win, int button );
    void (*scroll)( SingleWindow* win, int amount );

    uint32_t width_want, height_want; // zero for auto/max
    uint32_t anchor; // ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM|ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT
    uint32_t layer; // ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY
    char* name; // "awl_cal_popup"

    double continuous_event_norm; // 10.0;
};

static int allocate_shm_file(size_t size) {
    int fd = memfd_create("surface", MFD_CLOEXEC);
    if (fd == -1)
        return -1;
    int ret;
    do {
        ret = ftruncate(fd, size);
    } while (ret == -1 && errno == EINTR);
    if (ret == -1) {
        close(fd);
        return -1;
    }
    return fd;
}

static void pointer_enter(void *data, struct wl_pointer *pointer,
          uint32_t serial, struct wl_surface *surface,
          wl_fixed_t surface_x, wl_fixed_t surface_y);
static void pointer_leave(void *data, struct wl_pointer *pointer,
          uint32_t serial, struct wl_surface *surface);
static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
           uint32_t time, uint32_t button, uint32_t state);
static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
           wl_fixed_t surface_x, wl_fixed_t surface_y);
static void pointer_frame(void *data, struct wl_pointer *pointer);
static void pointer_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time,
               uint32_t axis, wl_fixed_t value);
static void pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
               uint32_t axis_source);
static void pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
               uint32_t time, uint32_t axis);
static void pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
               uint32_t axis, int32_t discrete);
static void pointer_axis_value120(void *data, struct wl_pointer *pointer,
              uint32_t axis, int32_t discrete);

static void seat_capabilities(void *data, struct wl_seat *wl_seat,
          uint32_t capabilities);
static void seat_name(void *data, struct wl_seat *wl_seat, const char *name);

static void teardown_window(SingleWindow *win);
static void teardown_seat(WaylandSeat *seat);

void window_refresh( Window* w ) {
    if (!w->has_init) return;
    SingleWindow* win;
    wl_list_for_each(win, &w->window_list, link)
        win->redraw = 1;
    eventfd_write(w->redraw_fd, 1);
}

void hide_window(SingleWindow *win) {
    if (!win->hidden) {
        zwlr_layer_surface_v1_destroy(win->layer_surface);
        wl_surface_destroy(win->wl_surface);

        win->configured = 0;
        win->hidden = 1;
    }
}

static void draw_window(SingleWindow *win) {
    if (!win) return;
    Window* w = win->parent;
    /* Allocate buffer to be attached to the surface */
    int fd = allocate_shm_file(win->bufsize);
    if (fd == -1) return;

    uint32_t *data = mmap(NULL, win->bufsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) return;

    struct wl_shm_pool *pool = wl_shm_create_pool(w->shm, fd, win->bufsize);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, win->width, win->height,
        win->stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    // pixman image
    pixman_image_t *final = pixman_image_create_bits(PIXMAN_a8r8g8b8, win->width, win->height, data, win->width * 4);

    // text layers
    pixman_image_t *foreground = pixman_image_create_bits(PIXMAN_a8r8g8b8, win->width, win->height, NULL, win->width * 4);
    pixman_image_t *background = pixman_image_create_bits(PIXMAN_a8r8g8b8, win->width, win->height, NULL, win->width * 4);

    if (win->parent->draw) (*win->parent->draw)( win, foreground, background );
    // XXX
    pixman_image_fill_boxes(PIXMAN_OP_SRC, background, &white, 1, &(pixman_box32_t){ .x1 = 0, .x2 = win->width, .y1 = 0, .y2 = win->height });

    /* Draw background and foreground on win */
    pixman_image_composite32(PIXMAN_OP_OVER, background, NULL, final, 0, 0, 0, 0, 0, 0, win->width, win->height);
    pixman_image_composite32(PIXMAN_OP_OVER, foreground, NULL, final, 0, 0, 0, 0, 0, 0, win->width, win->height);

    pixman_image_unref(foreground);
    pixman_image_unref(background);
    pixman_image_unref(final);

    munmap(data, win->bufsize);
    wl_surface_set_buffer_scale(win->wl_surface, w->buffer_scale);
    wl_surface_attach(win->wl_surface, buffer, 0, 0);
    wl_surface_damage_buffer(win->wl_surface, 0, 0, win->width, win->height);
    wl_surface_commit(win->wl_surface);
}

static void window_layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
            uint32_t serial, uint32_t w, uint32_t h) {
    SingleWindow *win = data;

    w = w * win->parent->buffer_scale;
    h = h * win->parent->buffer_scale;

    zwlr_layer_surface_v1_ack_configure(surface, serial);

    if (win->configured && w == win->width && h == win->height)
        return;

    win->width = w;
    win->height = h;
    win->stride = win->width * 4;
    win->bufsize = win->stride * win->height;
    win->configured = 1;

    draw_window(win);
}

static void window_layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *surface) {
    (void)surface;
    SingleWindow *win = data;
    hide_window(win);
}

static void show_window(SingleWindow *win) {
    Window* w = win->parent;
    win->wl_surface = wl_compositor_create_surface(w->compositor);
    if (!win->wl_surface)
        fprintf( stderr, "Could not create wl_surface" );

    win->layer_surface = zwlr_layer_shell_v1_get_layer_surface(w->layer_shell,
            win->wl_surface, win->wl_output, w->layer, w->name);
    if (!win->layer_surface)
        fprintf( stderr, "Could not create layer_surface" );
    zwlr_layer_surface_v1_add_listener(win->layer_surface, &w->layer_surface_listener, win);

    zwlr_layer_surface_v1_set_size(win->layer_surface, w->width_want/w->buffer_scale, w->height_want/w->buffer_scale);
    zwlr_layer_surface_v1_set_anchor(win->layer_surface, w->anchor);
    /* zwlr_layer_surface_v1_set_exclusive_zone(win->layer_surface, win->height / buffer_scale); */
    wl_surface_commit(win->wl_surface);
    win->hidden = 0;
}

static void setup_window(Window* w, SingleWindow *win) {
    win->hidden = w->hidden;
    if (!win->hidden)
        show_window(win);
}

static void window_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    DBG_MSG("%p %p %i %s %i", data, registry, name, interface, version);
    (void)version;
    Window* w = data;
    if (!strcmp(interface, wl_compositor_interface.name)) {
        w->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (!strcmp(interface, wl_shm_interface.name)) {
        w->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name)) {
        w->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
    } else if (!strcmp(interface, wl_output_interface.name)) {
        if (w->only_current_output) {
            if (!wl_list_length(&w->window_list)) {
                SingleWindow *win = calloc(1, sizeof(SingleWindow));
                win->parent = w;
                win->registry_name = name;
                if (w->running)
                    setup_window(w, win);
                wl_list_insert(&w->window_list, &win->link);
            }
        } else {
            SingleWindow *win = calloc(1, sizeof(SingleWindow));
            win->parent = w;
            win->registry_name = name;
            win->wl_output = wl_registry_bind(registry, name, &wl_output_interface, 1);
            if (w->running)
                setup_window(w, win);
            wl_list_insert(&w->window_list, &win->link);
        }
    } else if (!strcmp(interface, wl_seat_interface.name)) {
        if (w->only_current_output) {
            if (!wl_list_length(&w->seat_list)) {
                WaylandSeat *seat = calloc(1, sizeof(WaylandSeat));
                seat->continuous_event = 0.0;
                seat->continuous_event_norm = w->continuous_event_norm;
                seat->parent = w;
                seat->registry_name = name;
                seat->wl_seat = wl_registry_bind(registry, name, &wl_seat_interface, 7);
                wl_seat_add_listener(seat->wl_seat, &w->seat_listener, seat);
                wl_list_insert(&w->seat_list, &seat->link);
            }
        } else {
            WaylandSeat *seat = calloc(1, sizeof(WaylandSeat));
            seat->continuous_event = 0.0;
            seat->continuous_event_norm = w->continuous_event_norm;
            seat->parent = w;
            seat->registry_name = name;
            seat->wl_seat = wl_registry_bind(registry, name, &wl_seat_interface, 7);
            wl_seat_add_listener(seat->wl_seat, &w->seat_listener, seat);
            wl_list_insert(&w->seat_list, &seat->link);
        }
    }
}

static void window_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)registry;
    Window* w = data;
    SingleWindow *win;
    WaylandSeat *seat;
    wl_list_for_each(win, &w->window_list, link) {
        if (win->registry_name == name) {
            wl_list_remove(&win->link);
            teardown_window(win);
            return;
        }
    }
    wl_list_for_each(seat, &w->seat_list, link) {
        if (seat->registry_name == name) {
            wl_list_remove(&seat->link);
            teardown_seat(seat);
            return;
        }
    }
}


Window* window_setup( void ) {
    Window* w = calloc(1, sizeof(Window));
    w->buffer_scale = 2;
    w->redraw_fd = -1;

    // XXX change setup to be a bit more convenient
    w->hidden = 1;
    w->width_want = 128;
    w->height_want = 128;
    w->anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT|ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
    w->layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
    w->name = calloc(1,128);
    strcpy(w->name, "awl_cal_popup");
    w->continuous_event_norm = 10.0;
    w->only_current_output = 1;

    w->layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
    w->width_want = w->height_want = 0;

    w->display = wl_display_connect(NULL);
    if (!w->display) fprintf(stderr, "Failed to create display");
    wl_list_init(&w->window_list);
    wl_list_init(&w->seat_list);
    w->registry = wl_display_get_registry(w->display);
    w->registry_listener = (struct wl_registry_listener) {
        .global = window_handle_global,
        .global_remove = window_handle_global_remove
    };
    w->layer_surface_listener = (struct zwlr_layer_surface_v1_listener) {
        .configure = window_layer_surface_configure,
        .closed = window_layer_surface_closed,
    };
    w->pointer_listener = (struct wl_pointer_listener) {
        .axis = pointer_axis,
        .axis_discrete = pointer_axis_discrete,
        .axis_source = pointer_axis_source,
        .axis_stop = pointer_axis_stop,
        .axis_value120 = pointer_axis_value120,
        .button = pointer_button,
        .enter = pointer_enter,
        .frame = pointer_frame,
        .leave = pointer_leave,
        .motion = pointer_motion,
    };
    w->seat_listener = (struct wl_seat_listener) {
        .capabilities = seat_capabilities,
        .name = seat_name,
    };
    wl_registry_add_listener(w->registry, &w->registry_listener, w);
    wl_display_roundtrip(w->display);

    if (!w->compositor || !w->shm || !w->layer_shell)
        fprintf( stderr, "Compositor does not support all needed protocols" );

    /* Setup windows */
    SingleWindow* win;
    wl_list_for_each(win, &w->window_list, link)
        setup_window(w, win);
    wl_display_roundtrip(w->display);

    w->redraw_fd = eventfd(0,0);
    w->running = 1;
    w->has_init = 1;

    return w;
}

static void window_event_loop(Window* w) {
    int wl_fd = wl_display_get_fd(w->display);

    while (w->running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(wl_fd, &rfds);
        FD_SET(w->redraw_fd, &rfds);

        wl_display_flush(w->display);

        if (select(MAX(w->redraw_fd,wl_fd)+1, &rfds, NULL, NULL, NULL) == -1) {
            if (errno == EINTR)
                continue;
            else
                fprintf( stderr, "select" );
        }
        if (FD_ISSET(wl_fd, &rfds))
            if (wl_display_dispatch(w->display) == -1)
                break;
        if (FD_ISSET(w->redraw_fd, &rfds)) {
            uint64_t val = 0;
            eventfd_read(w->redraw_fd, &val);
        }

        SingleWindow *win;
        wl_list_for_each(win, &w->window_list, link) {
            if (win->redraw) {
                if (!win->hidden && win->configured)
                    draw_window(win);
                win->redraw = 0;
            }
        }
    }
}

void window_destroy( Window* w ) {
    w->has_init = 0;
    w->running = 0;
    eventfd_write(w->redraw_fd, 1);
    if (w->redraw_fd != -1) close( w->redraw_fd );

    SingleWindow *win, *win2;
    WaylandSeat *seat, *seat2;
    wl_list_for_each_safe(win, win2, &w->window_list, link)
        teardown_window(win);
    wl_list_for_each_safe(seat, seat2, &w->seat_list, link)
        teardown_seat(seat);

    zwlr_layer_shell_v1_destroy(w->layer_shell);

    wl_shm_destroy(w->shm);
    wl_compositor_destroy(w->compositor);
    wl_display_disconnect(w->display);
    if (w->name) free(w->name);
    free(w);
}

static void* thrf( void* arg ) {
    Window* w = arg;
    int rep = 10;
    while(rep--) {
        SingleWindow* www = NULL;
        SingleWindow* win;
        wl_list_for_each(win, &w->window_list, link) {
            www = win;
        }
        hide_window(www);
        window_refresh(w);
        usleep(2.e5);
        show_window(www);
        window_refresh(w);
        usleep(2.e5);
    }
    w->running = 0;
    window_refresh(w);
    return NULL;
}
int main( void ) {
    Window* w = window_setup();

    pthread_t thr;
    pthread_create( &thr, NULL, thrf, w );

    window_event_loop(w);

    pthread_join( thr, NULL );

    window_destroy(w);
}

static void pointer_enter(void *data, struct wl_pointer *pointer,
          uint32_t serial, struct wl_surface *surface,
          wl_fixed_t surface_x, wl_fixed_t surface_y) {
    (void)surface_x;
    (void)surface_y;
    WaylandSeat *seat = (WaylandSeat *)data;
    Window* wp = seat->parent;

    seat->win = NULL;
    SingleWindow *win;
    wl_list_for_each(win, &wp->window_list, link) {
        if (win->wl_surface == surface) {
            seat->win = win;
            break;
        }
    }

    if (!wp->cursor_image) {
        const char *size_str = getenv("XCURSOR_SIZE");
        int size = size_str ? atoi(size_str) : 0;
        if (size == 0)
            size = 24;
        struct wl_cursor_theme *cursor_theme = wl_cursor_theme_load(getenv("XCURSOR_THEME"),
                size * wp->buffer_scale, wp->shm);
        wp->cursor_image = wl_cursor_theme_get_cursor(cursor_theme, "left_ptr")->images[0];
        wp->cursor_surface = wl_compositor_create_surface(wp->compositor);
        wl_surface_set_buffer_scale(wp->cursor_surface, wp->buffer_scale);
        wl_surface_attach(wp->cursor_surface, wl_cursor_image_get_buffer(wp->cursor_image), 0, 0);
        wl_surface_commit(wp->cursor_surface);
    }
    wl_pointer_set_cursor(pointer, serial, wp->cursor_surface,
                  wp->cursor_image->hotspot_x,
                  wp->cursor_image->hotspot_y);
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
          uint32_t serial, struct wl_surface *surface) {
    (void)pointer;
    (void)serial;
    (void)surface;
    WaylandSeat *seat = (WaylandSeat *)data;

    seat->win = NULL;
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
           uint32_t time, uint32_t button, uint32_t state) {
    (void)pointer;
    (void)serial;
    (void)time;
    WaylandSeat *seat = (WaylandSeat *)data;

    seat->pointer_button = state == WL_POINTER_BUTTON_STATE_PRESSED ? button : 0;
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
           wl_fixed_t surface_x, wl_fixed_t surface_y) {
    (void)pointer;
    (void)time;
    WaylandSeat *seat = (WaylandSeat *)data;
    SingleWindow* win = seat->win;
    if (!win) return;

    seat->pointer_x = wl_fixed_to_int(surface_x);
    seat->pointer_y = wl_fixed_to_int(surface_y);
}

static void pointer_frame(void *data, struct wl_pointer *pointer) {
    (void)pointer;
    if (!data) return;
    WaylandSeat *seat = data;
    SingleWindow* win = seat->win;
    if (!win) return;
    int discrete_event = 0;
    struct PointerEvent* event = &seat->pointer_event;
    if (event->event_mask & (POINTER_EVENT_AXIS_DISCRETE|POINTER_EVENT_AXIS) &&
        event->axes[WL_POINTER_AXIS_VERTICAL_SCROLL].valid) {
        discrete_event = event->axes[WL_POINTER_AXIS_VERTICAL_SCROLL].discrete;
        if (!discrete_event) {
            seat->continuous_event += wl_fixed_to_double(event->axes[WL_POINTER_AXIS_VERTICAL_SCROLL].value) / seat->continuous_event_norm;
            if (fabs(seat->continuous_event) > 1.0) {
                discrete_event = seat->continuous_event;
                seat->continuous_event = 0.0;
            }
        }
    }
    memset(event, 0, sizeof(*event));

    Window* w = seat->parent;
    if (discrete_event && w->scroll) (*w->scroll)(win, discrete_event);
    if (seat->pointer_button && w->click) (*w->click)(win, seat->pointer_button);

}

static void pointer_axis(void *data, struct wl_pointer *wl_pointer, uint32_t time,
               uint32_t axis, wl_fixed_t value) {
    (void)wl_pointer;
    WaylandSeat *seat = (WaylandSeat*)data;
    seat->pointer_event.event_mask |= POINTER_EVENT_AXIS;
    seat->pointer_event.time = time;
    seat->pointer_event.axes[axis].valid = 1;
    seat->pointer_event.axes[axis].value = value;
}

static void pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
               uint32_t axis_source) {
    (void)wl_pointer;
    WaylandSeat *seat = (WaylandSeat*)data;
    seat->pointer_event.event_mask |= POINTER_EVENT_AXIS_SOURCE;
    seat->pointer_event.axis_source = axis_source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
               uint32_t time, uint32_t axis) {
    (void)wl_pointer;
    WaylandSeat *seat = (WaylandSeat*)data;
    seat->pointer_event.time = time;
    seat->pointer_event.event_mask |= POINTER_EVENT_AXIS_STOP;
    seat->pointer_event.axes[axis].valid = 1;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
               uint32_t axis, int32_t discrete) {
    (void)wl_pointer;
    WaylandSeat *seat = (WaylandSeat*)data;
    seat->pointer_event.event_mask |= POINTER_EVENT_AXIS_DISCRETE;
    seat->pointer_event.axes[axis].valid = 1;
    seat->pointer_event.axes[axis].discrete = discrete;
}

static void pointer_axis_value120(void *data, struct wl_pointer *pointer,
              uint32_t axis, int32_t discrete) {
    (void)data;
    (void)pointer;
    (void)axis;
    (void)discrete;
}

static void seat_capabilities(void *data, struct wl_seat *wl_seat,
          uint32_t capabilities) {
    (void)wl_seat;
    WaylandSeat *seat = (WaylandSeat *)data;
    uint32_t has_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;
    if (has_pointer && !seat->wl_pointer) {
        seat->wl_pointer = wl_seat_get_pointer(seat->wl_seat);
        wl_pointer_add_listener(seat->wl_pointer, &seat->parent->pointer_listener, seat);
    } else if (!has_pointer && seat->wl_pointer) {
        wl_pointer_destroy(seat->wl_pointer);
        seat->wl_pointer = NULL;
    }
}

static void seat_name(void *data, struct wl_seat *wl_seat, const char *name) {
    (void)data;
    (void)wl_seat;
    (void)name;
}

static void teardown_window(SingleWindow *win) {
    if (!win->hidden) {
        zwlr_layer_surface_v1_destroy(win->layer_surface);
        wl_surface_destroy(win->wl_surface);
    }
    if (win->wl_output) wl_output_destroy(win->wl_output);
    free(win);
}

static void teardown_seat(WaylandSeat *seat) {
    if (seat->wl_pointer)
        wl_pointer_destroy(seat->wl_pointer);
    wl_seat_destroy(seat->wl_seat);
    free(seat);
}

