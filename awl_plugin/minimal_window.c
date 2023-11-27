#include "minimal_window.h"

#include <linux/input-event-codes.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <pthread.h>
#include <utlist.h>
#include "../awl_log.h"
#include "../awl_state.h"
#include "init.h"
#include "../awl.h"

#include "xdg-shell-protocol.h"

enum pointer_event_mask {
    POINTER_EVENT_ENTER = 1 << 0,
    POINTER_EVENT_LEAVE = 1 << 1,
    POINTER_EVENT_MOTION = 1 << 2,
    POINTER_EVENT_BUTTON = 1 << 3,
    POINTER_EVENT_AXIS = 1 << 4,
    POINTER_EVENT_AXIS_SOURCE = 1 << 5,
    POINTER_EVENT_AXIS_STOP = 1 << 6,
    POINTER_EVENT_AXIS_DISCRETE = 1 << 7,
};

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

typedef struct WaylandSeat WaylandSeat;
struct WaylandSeat {
    struct wl_seat *wl_seat;
    struct wl_pointer *wl_pointer;
    uint32_t registry_name;

    AWL_SingleWindow *win;
    uint32_t pointer_x, pointer_y;
    uint32_t pointer_button;
    double continuous_event;
    double continuous_event_norm;
    struct PointerEvent pointer_event;

    AWL_Window* parent;

    WaylandSeat *prev, *next;
};

struct AWL_Window {
    int has_init;
    int redraw_fd;

    int only_current_output;
    int hidden;
    int running;
    pthread_mutex_t showmtx;

    uint32_t buffer_scale;

    struct wl_registry *registry;
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;

    struct wl_cursor_image *cursor_image;
    struct wl_surface *cursor_surface;

    AWL_SingleWindow* window_list;
    WaylandSeat* seat_list;

    struct zwlr_layer_surface_v1_listener layer_surface_listener;
    struct wl_pointer_listener pointer_listener;
    struct wl_seat_listener seat_listener;
    struct wl_registry_listener registry_listener;

    void (*draw)( AWL_SingleWindow* win, pixman_image_t* img );
    void (*click)( AWL_SingleWindow* win, int button );
    void (*scroll)( AWL_SingleWindow* win, int amount );

    uint32_t width_want, height_want; // zero for auto/max
    uint32_t anchor; // ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM|ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT
    uint32_t layer; // ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY
    char* name; // "awl_cal_popup"

    double continuous_event_norm; // 10.0;

    pthread_t event_thread;
    pthread_attr_t* event_thread_attr;
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

static void teardown_window(AWL_SingleWindow *win);
static void teardown_seat(WaylandSeat *seat);

void awl_minimal_window_refresh( AWL_Window* w ) {
    if (!w->has_init) return;
    AWL_SingleWindow* win;
    DL_FOREACH(w->window_list, win) {
        win->redraw = 1;
    }
    eventfd_write(w->redraw_fd, 1);
}

void hide_window(AWL_SingleWindow *win) {
    if (!win) return;
    AWL_Window* w = win->parent;
    if (!w) return;
    if (pthread_mutex_trylock( &w->showmtx ) == EBUSY) return;

    if (!win->hidden) {
        zwlr_layer_surface_v1_destroy(win->layer_surface);
        wl_surface_destroy(win->wl_surface);

        win->configured = 0;
        win->hidden = 1;
    }

    pthread_mutex_unlock( &w->showmtx );
}

static void draw_window(AWL_SingleWindow *win) {
    if (!win) return;
    AWL_Window* w = win->parent;
    if (!w) return;
    if (pthread_mutex_trylock( &w->showmtx ) == EBUSY) return;

    /* Allocate buffer to be attached to the surface */
    int fd = allocate_shm_file(win->bufsize);
    if (fd == -1) goto enddraw;

    uint32_t *data = mmap(NULL, win->bufsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) goto enddraw;

    struct wl_shm_pool *pool = wl_shm_create_pool(w->shm, fd, win->bufsize);
    struct wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, win->width, win->height,
        win->stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    // pixman image that's passed to the window drawing function
    pixman_image_t *final = pixman_image_create_bits(PIXMAN_a8r8g8b8, win->width, win->height, data, win->width * 4);
    if (win->parent->draw) (*win->parent->draw)( win, final );
    pixman_image_unref(final);

    munmap(data, win->bufsize);
    wl_surface_set_buffer_scale(win->wl_surface, w->buffer_scale);
    wl_surface_attach(win->wl_surface, buffer, 0, 0);
    wl_surface_damage_buffer(win->wl_surface, 0, 0, win->width, win->height);
    wl_surface_commit(win->wl_surface);
    wl_buffer_destroy(buffer);
enddraw:
    pthread_mutex_unlock( &w->showmtx );
}

static void window_layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *surface,
            uint32_t serial, uint32_t w, uint32_t h) {
    AWL_SingleWindow *win = data;

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
    AWL_SingleWindow *win = data;
    hide_window(win);
}

static void show_window(AWL_SingleWindow *win) {
    if (!win) return;
    AWL_Window* w = win->parent;
    if (!w) return;
    if (pthread_mutex_trylock( &w->showmtx ) == EBUSY) return;

    win->wl_surface = wl_compositor_create_surface(w->compositor);
    if (!win->wl_surface)
        P_awl_err_printf( "Could not create wl_surface" );

    static const char default_name[] = "awl_window";
    win->layer_surface = zwlr_layer_shell_v1_get_layer_surface(w->layer_shell,
            win->wl_surface, win->wl_output, w->layer, w->name ? w->name : default_name);
    if (!win->layer_surface)
        P_awl_err_printf( "Could not create layer_surface" );
    zwlr_layer_surface_v1_add_listener(win->layer_surface, &w->layer_surface_listener, win);

    zwlr_layer_surface_v1_set_size(win->layer_surface, w->width_want/w->buffer_scale, w->height_want/w->buffer_scale);
    zwlr_layer_surface_v1_set_anchor(win->layer_surface, w->anchor);
    /* zwlr_layer_surface_v1_set_exclusive_zone(win->layer_surface, win->height / buffer_scale); */
    wl_surface_commit(win->wl_surface);
    win->hidden = 0;
    pthread_mutex_unlock( &w->showmtx );
}

static void setup_window(AWL_Window* w, AWL_SingleWindow *win) {
    win->hidden = w->hidden;
    if (!win->hidden)
        show_window(win);
}

static void window_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    (void)version;
    AWL_Window* w = data;
    if (!strcmp(interface, wl_compositor_interface.name)) {
        w->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (!strcmp(interface, wl_shm_interface.name)) {
        w->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name)) {
        w->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
    } else if (!strcmp(interface, wl_output_interface.name)) {
        if (w->only_current_output) {
            AWL_SingleWindow* win;
            int counter;
            DL_COUNT(w->window_list, win, counter);
            if (!counter) {
                win = calloc(1, sizeof(AWL_SingleWindow));
                win->parent = w;
                win->registry_name = name;
                if (w->running)
                    setup_window(w, win);
                DL_APPEND(w->window_list, win);
            }
        } else {
            AWL_SingleWindow *win = calloc(1, sizeof(AWL_SingleWindow));
            win->parent = w;
            win->registry_name = name;
            win->wl_output = wl_registry_bind(registry, name, &wl_output_interface, 1);
            if (w->running)
                setup_window(w, win);
            DL_APPEND(w->window_list, win);
        }
    } else if (!strcmp(interface, wl_seat_interface.name)) {
        if (w->only_current_output) {
            WaylandSeat* seat;
            int counter;
            DL_COUNT(w->seat_list, seat, counter);
            if (!counter) {
                seat = calloc(1, sizeof(WaylandSeat));
                seat->continuous_event = 0.0;
                seat->continuous_event_norm = w->continuous_event_norm;
                seat->parent = w;
                seat->registry_name = name;
                seat->wl_seat = wl_registry_bind(registry, name, &wl_seat_interface, 7);
                wl_seat_add_listener(seat->wl_seat, &w->seat_listener, seat);
                DL_APPEND(w->seat_list, seat);
            }
        } else {
            WaylandSeat *seat = calloc(1, sizeof(WaylandSeat));
            seat->continuous_event = 0.0;
            seat->continuous_event_norm = w->continuous_event_norm;
            seat->parent = w;
            seat->registry_name = name;
            seat->wl_seat = wl_registry_bind(registry, name, &wl_seat_interface, 7);
            wl_seat_add_listener(seat->wl_seat, &w->seat_listener, seat);
            DL_APPEND(w->seat_list, seat);
        }
    }
}

static void window_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)registry;
    AWL_Window* w = data;
    AWL_SingleWindow *win, *win2;
    WaylandSeat *seat, *seat2;
    DL_FOREACH_SAFE(w->window_list, win, win2) {
        if (win->registry_name == name) {
            DL_DELETE(w->window_list, win);
            teardown_window(win);
            return;
        }
    }
    DL_FOREACH_SAFE(w->seat_list, seat, seat2) {
        if (seat->registry_name == name) {
            DL_DELETE(w->seat_list, seat);
            teardown_seat(seat);
            return;
        }
    }
}

static void* awl_minimal_window_event_loop_thread( void* arg );

AWL_Window* awl_minimal_window_setup( const awl_minimal_window_props_t* props ) {
    if (!props)
        props = &awl_minimal_window_props_defaults;

    AWL_Window* w = calloc(1, sizeof(AWL_Window));
    w->redraw_fd = -1;

    w->buffer_scale = props->buffer_scale;
    w->hidden = props->hidden;
    w->width_want = props->width_want;
    w->height_want = props->height_want;
    w->anchor = props->anchor;
    w->layer = props->layer;
    if (props->name)
        w->name = strdup(props->name);
    w->continuous_event_norm = props->continuous_event_norm;
    w->only_current_output = props->only_current_output;
    w->draw = props->draw;
    w->click = props->click;
    w->scroll = props->scroll;
    pthread_mutex_init( &w->showmtx, NULL );
    P_awl_log_printf( "create window event thread (%p)", w );
    w->event_thread_attr = calloc(1,sizeof(pthread_attr_t));
    pthread_attr_init( w->event_thread_attr );
    pthread_attr_setstacksize( w->event_thread_attr, 1024ul*1024ul*64ul );
    pthread_create( &w->event_thread, w->event_thread_attr, awl_minimal_window_event_loop_thread, w );
    return w;
}

static void awl_minimal_window_setup_async( AWL_Window* w ) {
    awl_state_t* B = NULL;
    while ((B = awl_plugin_state()) == NULL) {
        P_awl_err_printf( "could not find state (deadlock?)" );
        usleep(100);
    }
    while (!B->awl_is_ready()) usleep(100);
    w->display = wl_display_connect(NULL);
    if (!w->display) P_awl_err_printf("Failed to create display");
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
        P_awl_err_printf( "Compositor does not support all needed protocols" );

    /* Setup windows */
    AWL_SingleWindow* win;
    DL_FOREACH(w->window_list, win) {
        setup_window(w, win);
    }
    wl_display_roundtrip(w->display);

    w->redraw_fd = eventfd(0,0);
    w->has_init = 1;
}

static void* awl_minimal_window_event_loop_thread( void* arg ) {
    AWL_Window* w = arg;
    awl_minimal_window_setup_async( w );
    w->running = 1;

    int wl_fd = wl_display_get_fd(w->display);

    while (w->running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(wl_fd, &rfds);
        FD_SET(w->redraw_fd, &rfds);

        wl_display_flush(w->display);

        #define MAX( A, B ) ( (A) > (B) ? (A) : (B) )
        if (select(MAX(w->redraw_fd,wl_fd)+1, &rfds, NULL, NULL, NULL) == -1) {
            if (errno == EINTR)
                continue;
            else
                P_awl_err_printf( "select" );
        }
        if (FD_ISSET(wl_fd, &rfds))
            if (wl_display_dispatch(w->display) == -1)
                break;
        if (FD_ISSET(w->redraw_fd, &rfds)) {
            uint64_t val = 0;
            eventfd_read(w->redraw_fd, &val);
        }

        AWL_SingleWindow *win;
        DL_FOREACH(w->window_list, win) {
            if (win->redraw) {
                if (!win->hidden && win->configured)
                    draw_window(win);
                win->redraw = 0;
            }
        }
    }
    pthread_exit(NULL);
    return NULL;
}

void awl_minimal_window_destroy( AWL_Window* w ) {
    P_awl_log_printf( "in minimal window destroy (%p)", w );
    awl_minimal_window_wait_ready(w);

    pthread_mutex_lock( &w->showmtx );
    w->draw = NULL;
    pthread_mutex_unlock( &w->showmtx );

    if (w->running) {
        w->running = 0;
        pthread_mutex_lock( &w->showmtx );
        AWL_SingleWindow *win;
        DL_FOREACH(w->window_list, win) {
            win->redraw = 0;
        }
        eventfd_write( w->redraw_fd, 1 );
        pthread_mutex_unlock( &w->showmtx );
        pthread_join( w->event_thread, NULL );
    }
    if (w->event_thread_attr) pthread_attr_destroy( w->event_thread_attr );
    free( w->event_thread_attr );
    w->has_init = 0;
    if (w->redraw_fd != -1) close( w->redraw_fd );

    pthread_mutex_trylock( &w->showmtx );
    pthread_mutex_unlock( &w->showmtx );
    pthread_mutex_destroy( &w->showmtx );

    AWL_SingleWindow *win, *win2;
    WaylandSeat *seat, *seat2;
    DL_FOREACH_SAFE(w->window_list, win, win2) {
        DL_DELETE(w->window_list, win);
        teardown_window(win);
    }
    DL_FOREACH_SAFE(w->seat_list, seat, seat2) {
        DL_DELETE(w->seat_list, seat);
        teardown_seat(seat);
    }

    zwlr_layer_shell_v1_destroy(w->layer_shell);

    wl_shm_destroy(w->shm);
    wl_compositor_destroy(w->compositor);
    wl_display_disconnect(w->display);
    if (w->name) free(w->name);
    free(w);
}

static void pointer_enter(void *data, struct wl_pointer *pointer,
          uint32_t serial, struct wl_surface *surface,
          wl_fixed_t surface_x, wl_fixed_t surface_y) {
    (void)surface_x;
    (void)surface_y;
    WaylandSeat *seat = (WaylandSeat *)data;
    AWL_Window* wp = seat->parent;

    seat->win = NULL;
    AWL_SingleWindow *win;
    DL_FOREACH(wp->window_list, win) {
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
    AWL_SingleWindow* win = seat->win;
    if (!win) return;

    seat->pointer_x = wl_fixed_to_int(surface_x);
    seat->pointer_y = wl_fixed_to_int(surface_y);
}

static void pointer_frame(void *data, struct wl_pointer *pointer) {
    (void)pointer;
    if (!data) return;
    WaylandSeat *seat = data;
    AWL_SingleWindow* win = seat->win;
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

    AWL_Window* w = seat->parent;
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

static void teardown_window(AWL_SingleWindow *win) {
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

void awl_minimal_window_hide( AWL_Window* w ) {
    awl_minimal_window_wait_ready(w);
    AWL_SingleWindow* win;
    DL_FOREACH(w->window_list, win) {
        hide_window(win);
    }
    awl_minimal_window_refresh(w);
}

void awl_minimal_window_show( AWL_Window* w ) {
    awl_minimal_window_wait_ready(w);
    AWL_SingleWindow* win;
    DL_FOREACH(w->window_list, win) {
        show_window(win);
    }
    awl_minimal_window_refresh(w);
}

int awl_minimal_window_is_hidden( AWL_Window* w ) {
    awl_minimal_window_wait_ready(w);
    int hidden = 0;
    AWL_SingleWindow* win;
    DL_FOREACH(w->window_list, win) {
        hidden += win->hidden;
    }
    return hidden;
}

void awl_minimal_window_resize( AWL_Window* W, int w, int h ) {
    W->width_want = w;
    W->height_want = h;
}

void awl_minimal_window_wait_ready( AWL_Window* w ) {
    while (!w->has_init) usleep(1000);
}

