#include "awl_dbus.h"
#include "awl_log.h"

#include <uthash.h>
#include <dbus/dbus.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>

typedef struct {
    awl_dbus_hook_t hook;
    void* userdata;

    char name[1024];
    UT_hash_handle hh;
} awl_dbus_callback_t;

struct awl_dbus_listener_t {
    awl_dbus_callback_t* callbacks;

    int running;

    pthread_t receiver;
    pthread_mutex_t mtx;
};

static void* receiver_thread(void* handle_) {
    awl_dbus_listener_t* handle = (awl_dbus_listener_t*)handle_;
    DBusMessage* msg;
    DBusMessageIter args;
    DBusConnection* conn;
    DBusError err;
    int ret;
    char* sigvalue;

    awl_log_printf("dbus: listening for signals");

    // initialise the errors
    dbus_error_init(&err);

    // connect to the bus and check for errors
    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        awl_err_printf( "dbus connection Error (%s)", err.message );
        dbus_error_free(&err);
    }
    if (NULL == conn) return NULL;

    // request our name on the bus and check for errors
    ret = dbus_bus_request_name(conn, AWL_DBUS_ROOT_PATH ".sink", DBUS_NAME_FLAG_REPLACE_EXISTING , &err);
    if (dbus_error_is_set(&err)) {
        awl_err_printf( "dbus Name Error (%s)", err.message);
        dbus_error_free(&err);
    }
    if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != ret) return NULL;

    // add a rule for which messages we want to see
    dbus_bus_add_match(conn, "type='signal',interface='" AWL_DBUS_ROOT_PATH ".Type'", &err); // see signals from the given interface
    dbus_connection_flush(conn);
    if (dbus_error_is_set(&err)) {
        awl_err_printf( "dbus Match Error (%s)", err.message);
        return NULL;
    }
    awl_log_printf("dbus Match rule sent");

    // loop listening for signals being emmitted
    while (handle->running) {

        // non blocking read of the next available message
        dbus_connection_read_write(conn, 0);
        msg = dbus_connection_pop_message(conn);

        // loop again if we haven't read a message
        if (NULL == msg) {
           usleep(10000);
           continue;
        }

        if (msg)
            awl_log_printf("found message with path %s", dbus_message_get_path(msg));
        pthread_mutex_lock(&handle->mtx);

        awl_dbus_callback_t *s, *tmp;
        HASH_ITER(hh, handle->callbacks, s, tmp) {
            if (dbus_message_is_signal(msg, AWL_DBUS_ROOT_PATH ".Type", s->name)) {
                // read the parameters
                if (!dbus_message_iter_init(msg, &args)) {
                   awl_err_printf("dbus: Message Has No Parameters");
                } else if (DBUS_TYPE_STRING != dbus_message_iter_get_arg_type(&args)) {
                   awl_err_printf("dbus: Argument is not string!");
                } else {
                   dbus_message_iter_get_basic(&args, &sigvalue);
                }

                awl_log_printf("dbus: Got Signal with value %s", sigvalue);
                awl_log_printf("dbus: calling hook");
                s->hook( sigvalue, s->userdata );
            }
        }
        pthread_mutex_unlock(&handle->mtx);

        // free the message
        dbus_message_unref(msg);
    }
    return NULL;
}

awl_dbus_listener_t* awl_dbus_init( void ) {
    awl_dbus_listener_t* h = calloc(1, sizeof(awl_dbus_listener_t));
    h->running = 1;
    h->callbacks = NULL;
    pthread_mutex_init( &h->mtx, NULL );
    pthread_create( &h->receiver, NULL, &receiver_thread, h );
    return h;
}

void awl_dbus_add_callback( awl_dbus_listener_t* bus, const char* name_, awl_dbus_hook_t hook, void* userdata ) {

    pthread_mutex_lock( &bus->mtx );

    awl_dbus_callback_t* s = NULL;
    HASH_FIND_STR(bus->callbacks, name_, s);
    if (s) {
        awl_log_printf("dbus callback %s already in use", name_);
        goto end_add_callback;
    }

    s = calloc(1, sizeof(awl_dbus_callback_t));
    s->hook = hook;
    strncpy( s->name, name_, 1023 );
    s->userdata = userdata;

    HASH_ADD_STR( bus->callbacks, name, s );

end_add_callback:
    pthread_mutex_unlock( &bus->mtx );
}

void awl_dbus_remove_callback( awl_dbus_listener_t* bus, const char* name_ ) {
    pthread_mutex_lock( &bus->mtx );
    awl_dbus_callback_t* s = NULL;

    HASH_FIND_STR(bus->callbacks, name_, s);
    if (s) {
        HASH_DEL(bus->callbacks, s);
        free(s);
    } else {
        awl_log_printf( "could not find callback %s", name_ );
    }
    pthread_mutex_unlock( &bus->mtx );
}

void awl_dbus_destroy( awl_dbus_listener_t* bus ) {
    bus->running = 0;
    pthread_join( bus->receiver, NULL );

    pthread_mutex_lock( &bus->mtx );
    awl_dbus_callback_t *s, *tmp;
    HASH_ITER(hh, bus->callbacks, s, tmp) {
        HASH_DEL(bus->callbacks, s);
        free(s);
    }
    HASH_CLEAR(hh, bus->callbacks);
    pthread_mutex_unlock( &bus->mtx );

    pthread_mutex_destroy( &bus->mtx );
    free(bus);
}

void awl_dbus_notify( const char* name, const char* sigvalue ) {
    DBusMessage* msg;
    DBusMessageIter args;
    DBusConnection* conn;
    DBusError err;
    int ret;
    dbus_uint32_t serial = 0;

    awl_log_printf("dbus: sending signal with value %s", sigvalue);

    // initialise the error value
    dbus_error_init(&err);

    // connect to the DBUS system bus, and check for errors
    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        awl_err_printf( "dbus connection error (%s)", err.message );
        dbus_error_free(&err);
    }
    if (NULL == conn) return;

    // register our name on the bus, and check for errors
    ret = dbus_bus_request_name(conn, AWL_DBUS_ROOT_PATH ".source", DBUS_NAME_FLAG_REPLACE_EXISTING , &err);
    if (dbus_error_is_set(&err)) {
        awl_err_printf( "name error (%s)", err.message );
        dbus_error_free(&err);
    }
    if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != ret) return;

    // create a signal & check for errors
    char* object = calloc(1, strlen(AWL_DBUS_ROOT_PATH ".Type") + 20);
    strcpy( object, "/" );
    strcat( object, AWL_DBUS_ROOT_PATH ".Object" );
    for (char* c = object; *c; c++) if (*c == '.') *c = '/';
    msg = dbus_message_new_signal(object, AWL_DBUS_ROOT_PATH ".Type", name);
    free(object);
    if (NULL == msg) {
        awl_err_printf("dbus: Message Null");
        return;
    }

    // append arguments onto signal
    dbus_message_iter_init_append(msg, &args);
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &sigvalue)) {
        awl_err_printf("dbus: Out Of Memory!");
        return;
    }

    // send the message and flush the connection
    if (!dbus_connection_send(conn, msg, &serial)) {
        awl_err_printf("dbus: Out Of Memory!");
        return;
    }
    dbus_connection_flush(conn);

    awl_log_printf("Signal Sent");

    // free the message
    dbus_message_unref(msg);
}
