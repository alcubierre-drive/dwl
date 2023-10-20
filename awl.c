#include "dwl.h"
#include "config.h"
#include "client.h"

#include "dwl-ipc-unstable-v2-protocol.h"
#include "util.h"

void chvt(unsigned i) {
    wlr_session_change_vt(wlr_backend_get_session(backend), i);
}

void createkeyboard(struct wlr_keyboard *keyboard) {
    struct xkb_context *context;
    struct xkb_keymap *keymap;
    Keyboard *kb = keyboard->data = ecalloc(1, sizeof(*kb));
    kb->wlr_keyboard = keyboard;

    /* Prepare an XKB keymap and assign it to the keyboard. */
    context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    keymap = xkb_keymap_new_from_names(context, &xkb_rules,
        XKB_KEYMAP_COMPILE_NO_FLAGS);

    wlr_keyboard_set_keymap(keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(keyboard, repeat_rate, repeat_delay);

    /* Here we set up listeners for keyboard events. */
    LISTEN(&keyboard->events.modifiers, &kb->modifiers, keypressmod);
    LISTEN(&keyboard->events.key, &kb->key, keypress);
    LISTEN(&keyboard->base.events.destroy, &kb->destroy, cleanupkeyboard);

    wlr_seat_set_keyboard(seat, keyboard);

    kb->key_repeat_source = wl_event_loop_add_timer(
            wl_display_get_event_loop(dpy), keyrepeat, kb);

    /* And add the keyboard to our list of keyboards */
    wl_list_insert(&keyboards, &kb->link);
}

void createpointer(struct wlr_pointer *pointer) {
    if (wlr_input_device_is_libinput(&pointer->base)) {
        struct libinput_device *libinput_device = (struct libinput_device*)
            wlr_libinput_get_device_handle(&pointer->base);

        if (libinput_device_config_tap_get_finger_count(libinput_device)) {
            libinput_device_config_tap_set_enabled(libinput_device, tap_to_click);
            libinput_device_config_tap_set_drag_enabled(libinput_device, tap_and_drag);
            libinput_device_config_tap_set_drag_lock_enabled(libinput_device, drag_lock);
            libinput_device_config_tap_set_button_map(libinput_device, button_map);
        }

        if (libinput_device_config_scroll_has_natural_scroll(libinput_device))
            libinput_device_config_scroll_set_natural_scroll_enabled(libinput_device, natural_scrolling);

        if (libinput_device_config_dwt_is_available(libinput_device))
            libinput_device_config_dwt_set_enabled(libinput_device, disable_while_typing);

        if (libinput_device_config_left_handed_is_available(libinput_device))
            libinput_device_config_left_handed_set(libinput_device, left_handed);

        if (libinput_device_config_middle_emulation_is_available(libinput_device))
            libinput_device_config_middle_emulation_set_enabled(libinput_device, middle_button_emulation);

        if (libinput_device_config_scroll_get_methods(libinput_device) != LIBINPUT_CONFIG_SCROLL_NO_SCROLL)
            libinput_device_config_scroll_set_method (libinput_device, scroll_method);

        if (libinput_device_config_click_get_methods(libinput_device) != LIBINPUT_CONFIG_CLICK_METHOD_NONE)
            libinput_device_config_click_set_method (libinput_device, click_method);

        if (libinput_device_config_send_events_get_modes(libinput_device))
            libinput_device_config_send_events_set_mode(libinput_device, send_events_mode);

        if (libinput_device_config_accel_is_available(libinput_device)) {
            libinput_device_config_accel_set_profile(libinput_device, accel_profile);
            libinput_device_config_accel_set_speed(libinput_device, accel_speed);
        }
    }

    wlr_cursor_attach_input_device(cursor, &pointer->base);
}

void cursorframe(struct wl_listener *listener, void *data) {
    /* This event is forwarded by the cursor when a pointer emits an frame
     * event. Frame events are sent after regular pointer events to group
     * multiple events together. For instance, two axis events may happen at the
     * same time, in which case a frame event won't be sent in between. */
    /* Notify the client with pointer focus of the frame event. */
    wlr_seat_pointer_notify_frame(seat);
}

void
handlesig(int signo)
{
    if (signo == SIGCHLD) {
#ifdef XWAYLAND
        siginfo_t in;
        /* wlroots expects to reap the XWayland process itself, so we
         * use WNOWAIT to keep the child waitable until we know it's not
         * XWayland.
         */
        while (!waitid(P_ALL, 0, &in, WEXITED|WNOHANG|WNOWAIT) && in.si_pid
                && (!xwayland || in.si_pid != xwayland->server->pid))
            waitpid(in.si_pid, NULL, 0);
#else
        while (waitpid(-1, NULL, WNOHANG) > 0);
#endif
    } else if (signo == SIGINT || signo == SIGTERM) {
        quit(NULL);
    }
}

void
inputdevice(struct wl_listener *listener, void *data)
{
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
    if (!wl_list_empty(&keyboards))
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(seat, caps);
}

int
keybinding(uint32_t mods, xkb_keysym_t sym) {
    /*
     * Here we handle compositor keybindings. This is when the compositor is
     * processing keys, rather than passing them on to the client for its own
     * processing.
     */
    int handled = 0,
        ext = 0;
    const Key* kstart = keys;
    int nk = LENGTH(keys);
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
        kstart = extension_keys( &nk );
        if (kstart != NULL)
            goto keyhandle;
    }
    return handled;
}

void
keypress(struct wl_listener *listener, void *data)
{
    int i;
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
    if (!locked && !input_inhibit_mgr->active_inhibitor
            && event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
        for (i = 0; i < nsyms; i++)
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
    wlr_seat_set_keyboard(seat, kb->wlr_keyboard);
    wlr_seat_keyboard_notify_key(seat, event->time_msec,
        event->keycode, event->state);
}

void
keypressmod(struct wl_listener *listener, void *data)
{
    /* This event is raised when a modifier key, such as shift or alt, is
     * pressed. We simply communicate this to the client. */
    Keyboard *kb = wl_container_of(listener, kb, modifiers);
    /*
     * A seat can only have one keyboard, but this is a limitation of the
     * Wayland protocol - not wlroots. We assign all connected keyboards to the
     * same seat. You can swap out the underlying wlr_keyboard like this and
     * wlr_seat handles this transparently.
     */
    wlr_seat_set_keyboard(seat, kb->wlr_keyboard);
    /* Send modifiers to the client. */
    wlr_seat_keyboard_notify_modifiers(seat,
        &kb->wlr_keyboard->modifiers);
}

int
keyrepeat(void *data)
{
    Keyboard *kb = data;
    int i;
    if (!kb->nsyms || kb->wlr_keyboard->repeat_info.rate <= 0)
        return 0;

    wl_event_source_timer_update(kb->key_repeat_source,
            1000 / kb->wlr_keyboard->repeat_info.rate);

    for (i = 0; i < kb->nsyms; i++)
        keybinding(kb->mods, kb->keysyms[i]);

    return 0;
}

void
quit(const Arg *arg)
{
    wl_display_terminate(dpy);
}

void run(char *startup_cmd) {
    /* Add a Unix socket to the Wayland display. */
    const char *socket = wl_display_add_socket_auto(dpy);
    if (!socket)
        die("startup: display_add_socket_auto");
    setenv("WAYLAND_DISPLAY", socket, 1);

    /* Start the backend. This will enumerate outputs and inputs, become the DRM
     * master, etc */
    if (!wlr_backend_start(backend))
        die("startup: backend_start");

    /* Now that the socket exists and the backend is started, run the startup command */
    if (startup_cmd) {
        int piperw[2];
        if (pipe(piperw) < 0)
            die("startup: pipe:");
        if ((child_pid = fork()) < 0)
            die("startup: fork:");
        if (child_pid == 0) {
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
    selmon = xytomon(cursor->x, cursor->y);

    /* TODO hack to get cursor to display in its initial location (100, 100)
     * instead of (0, 0) and then jumping. still may not be fully
     * initialized, as the image/coordinates are not transformed for the
     * monitor when displayed here */
    wlr_cursor_warp_closest(cursor, NULL, cursor->x, cursor->y);
    wlr_xcursor_manager_set_cursor_image(cursor_mgr, cursor_image, cursor);

    /* Run the Wayland event loop. This does not return until you exit the
     * compositor. Starting the backend rigged up all of the necessary event
     * loop configuration to listen to libinput events, DRM events, generate
     * frame events at the refresh rate, and so on. */
    wl_display_run(dpy);
}

void setup(void) {
    int i, sig[] = {SIGCHLD, SIGINT, SIGTERM, SIGPIPE};
    struct sigaction sa = {.sa_flags = SA_RESTART, .sa_handler = handlesig};
    sigemptyset(&sa.sa_mask);

    for (i = 0; i < LENGTH(sig); i++)
        sigaction(sig[i], &sa, NULL);

    wlr_log_init(log_level, NULL);

    /* The Wayland display is managed by libwayland. It handles accepting
     * clients from the Unix socket, manging Wayland globals, and so on. */
    dpy = wl_display_create();

    /* The backend is a wlroots feature which abstracts the underlying input and
     * output hardware. The autocreate option will choose the most suitable
     * backend based on the current environment, such as opening an X11 window
     * if an X11 server is running. The NULL argument here optionally allows you
     * to pass in a custom renderer if wlr_renderer doesn't meet your needs. The
     * backend uses the renderer, for example, to fall back to software cursors
     * if the backend does not support hardware cursors (some older GPUs
     * don't). */
    if (!(backend = wlr_backend_autocreate(dpy)))
        die("couldn't create backend");

    /* Initialize the scene graph used to lay out windows */
    scene = wlr_scene_create();
    for (i = 0; i < NUM_LAYERS; i++)
        layers[i] = wlr_scene_tree_create(&scene->tree);
    drag_icon = wlr_scene_tree_create(&scene->tree);
    wlr_scene_node_place_below(&drag_icon->node, &layers[LyrBlock]->node);

    /* Create a renderer with the default implementation */
    if (!(drw = wlr_renderer_autocreate(backend)))
        die("couldn't create renderer");
    wlr_renderer_init_wl_display(drw, dpy);

    /* Create a default allocator */
    if (!(alloc = wlr_allocator_autocreate(backend, drw)))
        die("couldn't create allocator");

    /* This creates some hands-off wlroots interfaces. The compositor is
     * necessary for clients to allocate surfaces and the data device manager
     * handles the clipboard. Each of these wlroots interfaces has room for you
     * to dig your fingers in and play with their behavior if you want. Note that
     * the clients cannot set the selection directly without compositor approval,
     * see the setsel() function. */
    compositor = wlr_compositor_create(dpy, drw);
    wlr_export_dmabuf_manager_v1_create(dpy);
    wlr_screencopy_manager_v1_create(dpy);
    wlr_data_control_manager_v1_create(dpy);
    wlr_data_device_manager_create(dpy);
    wlr_gamma_control_manager_v1_create(dpy);
    wlr_primary_selection_v1_device_manager_create(dpy);
    wlr_viewporter_create(dpy);
    wlr_single_pixel_buffer_manager_v1_create(dpy);
    wlr_subcompositor_create(dpy);

    /* Initializes the interface used to implement urgency hints */
    activation = wlr_xdg_activation_v1_create(dpy);
    wl_signal_add(&activation->events.request_activate, &request_activate);

    /* Creates an output layout, which a wlroots utility for working with an
     * arrangement of screens in a physical layout. */
    output_layout = wlr_output_layout_create();
    wl_signal_add(&output_layout->events.change, &layout_change);
    wlr_xdg_output_manager_v1_create(dpy, output_layout);

    /* Configure a listener to be notified when new outputs are available on the
     * backend. */
    wl_list_init(&mons);
    wl_signal_add(&backend->events.new_output, &new_output);

    /* Set up our client lists and the xdg-shell. The xdg-shell is a
     * Wayland protocol which is used for application windows. For more
     * detail on shells, refer to the article:
     *
     * https://drewdevault.com/2018/07/29/Wayland-shells.html
     */
    wl_list_init(&clients);
    wl_list_init(&fstack);

    idle = wlr_idle_create(dpy);
    idle_notifier = wlr_idle_notifier_v1_create(dpy);

    idle_inhibit_mgr = wlr_idle_inhibit_v1_create(dpy);
    wl_signal_add(&idle_inhibit_mgr->events.new_inhibitor, &idle_inhibitor_create);

    layer_shell = wlr_layer_shell_v1_create(dpy);
    wl_signal_add(&layer_shell->events.new_surface, &new_layer_shell_surface);

    xdg_shell = wlr_xdg_shell_create(dpy, 4);
    wl_signal_add(&xdg_shell->events.new_surface, &new_xdg_surface);

    input_inhibit_mgr = wlr_input_inhibit_manager_create(dpy);
    session_lock_mgr = wlr_session_lock_manager_v1_create(dpy);
    wl_signal_add(&session_lock_mgr->events.new_lock, &session_lock_create_lock);
    wl_signal_add(&session_lock_mgr->events.destroy, &session_lock_mgr_destroy);
    locked_bg = wlr_scene_rect_create(layers[LyrBlock], sgeom.width, sgeom.height,
            (float [4]){0.1, 0.1, 0.1, 1.0});
    wlr_scene_node_set_enabled(&locked_bg->node, 0);

    /* Use decoration protocols to negotiate server-side decorations */
    wlr_server_decoration_manager_set_default_mode(
            wlr_server_decoration_manager_create(dpy),
            WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
    xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(dpy);
    wl_signal_add(&xdg_decoration_mgr->events.new_toplevel_decoration, &new_xdg_decoration);

    /*
     * Creates a cursor, which is a wlroots utility for tracking the cursor
     * image shown on screen.
     */
    cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(cursor, output_layout);

    /* Creates an xcursor manager, another wlroots utility which loads up
     * Xcursor themes to source cursor images from and makes sure that cursor
     * images are available at all scale factors on the screen (necessary for
     * HiDPI support). Scaled cursors will be loaded with each output. */
    cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
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
    wl_signal_add(&cursor->events.motion, &cursor_motion);
    wl_signal_add(&cursor->events.motion_absolute, &cursor_motion_absolute);
    wl_signal_add(&cursor->events.button, &cursor_button);
    wl_signal_add(&cursor->events.axis, &cursor_axis);
    wl_signal_add(&cursor->events.frame, &cursor_frame);

    /*
     * Configures a seat, which is a single "seat" at which a user sits and
     * operates the computer. This conceptually includes up to one keyboard,
     * pointer, touch, and drawing tablet device. We also rig up a listener to
     * let us know when new input devices are available on the backend.
     */
    wl_list_init(&keyboards);
    wl_signal_add(&backend->events.new_input, &new_input);
    virtual_keyboard_mgr = wlr_virtual_keyboard_manager_v1_create(dpy);
    wl_signal_add(&virtual_keyboard_mgr->events.new_virtual_keyboard,
            &new_virtual_keyboard);
    seat = wlr_seat_create(dpy, "seat0");
    wl_signal_add(&seat->events.request_set_cursor, &request_cursor);
    wl_signal_add(&seat->events.request_set_selection, &request_set_sel);
    wl_signal_add(&seat->events.request_set_primary_selection, &request_set_psel);
    wl_signal_add(&seat->events.request_start_drag, &request_start_drag);
    wl_signal_add(&seat->events.start_drag, &start_drag);

    output_mgr = wlr_output_manager_v1_create(dpy);
    wl_signal_add(&output_mgr->events.apply, &output_mgr_apply);
    wl_signal_add(&output_mgr->events.test, &output_mgr_test);

    wlr_scene_set_presentation(scene, wlr_presentation_create(dpy, backend));
    wl_global_create(dpy, &zdwl_ipc_manager_v2_interface, 2, NULL, dwl_ipc_manager_bind);

#ifdef XWAYLAND
    /*
     * Initialise the XWayland X server.
     * It will be started when the first X client is started.
     */
    xwayland = wlr_xwayland_create(dpy, compositor, 1);
    if (xwayland) {
        wl_signal_add(&xwayland->events.ready, &xwayland_ready);
        wl_signal_add(&xwayland->events.new_surface, &new_xwayland_surface);

        setenv("DISPLAY", xwayland->display_name, 1);
    } else {
        fprintf(stderr, "failed to setup XWayland X server, continuing without it\n");
    }
#endif
}

#ifdef XWAYLAND
void
activatex11(struct wl_listener *listener, void *data)
{
    Client *c = wl_container_of(listener, c, activate);

    /* Only "managed" windows can be activated */
    if (c->type == X11Managed)
        wlr_xwayland_surface_activate(c->surface.xwayland, 1);
}

void
configurex11(struct wl_listener *listener, void *data)
{
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

void
createnotifyx11(struct wl_listener *listener, void *data)
{
    struct wlr_xwayland_surface *xsurface = data;
    Client *c;

    /* Allocate a Client for this surface */
    c = xsurface->data = ecalloc(1, sizeof(*c));
    c->surface.xwayland = xsurface;
    c->type = xsurface->override_redirect ? X11Unmanaged : X11Managed;
    c->bw = borderpx;

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

xcb_atom_t
getatom(xcb_connection_t *xc, const char *name)
{
    xcb_atom_t atom = 0;
    xcb_intern_atom_reply_t *reply;
    xcb_intern_atom_cookie_t cookie = xcb_intern_atom(xc, 0, strlen(name), name);
    if ((reply = xcb_intern_atom_reply(xc, cookie, NULL)))
        atom = reply->atom;
    free(reply);

    return atom;
}

void
sethints(struct wl_listener *listener, void *data)
{
    Client *c = wl_container_of(listener, c, set_hints);
    if (c == focustop(selmon))
        return;

    c->isurgent = xcb_icccm_wm_hints_get_urgency(c->surface.xwayland->hints);

    if (c->isurgent && client_is_mapped(c))
        client_set_border_color(c, urgentcolor);

    printstatus();
}

void
xwaylandready(struct wl_listener *listener, void *data)
{
    struct wlr_xcursor *xcursor;
    xcb_connection_t *xc = xcb_connect(xwayland->display_name, NULL);
    int err = xcb_connection_has_error(xc);
    if (err) {
        fprintf(stderr, "xcb_connect to X server failed with code %d\n. Continuing with degraded functionality.\n", err);
        return;
    }

    /* Collect atoms we are interested in. If getatom returns 0, we will
     * not detect that window type. */
    netatom[NetWMWindowTypeDialog] = getatom(xc, "_NET_WM_WINDOW_TYPE_DIALOG");
    netatom[NetWMWindowTypeSplash] = getatom(xc, "_NET_WM_WINDOW_TYPE_SPLASH");
    netatom[NetWMWindowTypeToolbar] = getatom(xc, "_NET_WM_WINDOW_TYPE_TOOLBAR");
    netatom[NetWMWindowTypeUtility] = getatom(xc, "_NET_WM_WINDOW_TYPE_UTILITY");

    /* assign the one and only seat */
    wlr_xwayland_set_seat(xwayland, seat);

    /* Set the default XWayland cursor to match the rest of dwl. */
    if ((xcursor = wlr_xcursor_manager_get_xcursor(cursor_mgr, "left_ptr", 1)))
        wlr_xwayland_set_cursor(xwayland,
                xcursor->images[0]->buffer, xcursor->images[0]->width * 4,
                xcursor->images[0]->width, xcursor->images[0]->height,
                xcursor->images[0]->hotspot_x, xcursor->images[0]->hotspot_y);

    xcb_disconnect(xc);
}
#endif

int main(int argc, char **) {
    char startup_cmd[1024] = {0};
    int c;

    while ((c = getopt(argc, argv, "s:hdv")) != -1) {
        switch (c) {
            case 's': strcpy( startup_cmd, optarg ); break;
            case 'd': log_level = WLR_DEBUG; break;
            case 'v': die("dwl " VERSION); break;
            default: goto usage; break;
        }
    }
    if (optind < argc)
        goto usage;

    // what do we need here?
    // - Xwayland init
    // - Keyboard init
    // - Insert *important* keyboard combinations (those that are *not*
    //   configurable at run time: reload, quit, chvt)
    // - Set up plugin infrastructure
    // - Load the extension library that can be reloaded
    extension_init();
    /* Wayland requires XDG_RUNTIME_DIR for creating its communications socket */
    if (!getenv("XDG_RUNTIME_DIR"))
        die("XDG_RUNTIME_DIR must be set");
    setup();
    run(startup_cmd);
    cleanup();

    extension_close();
    return EXIT_SUCCESS;

usage:
    die("Usage: %s [-v] [-d] [-s startup command]", argv[0]);
}

