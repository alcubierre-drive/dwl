#include "awl_dbus.h"
#include "awl_pthread.h"
#include "awl_log.h"

#include <uthash.h>
#include <dbus/dbus.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>

typedef struct {
    awl_dbus_hook_t hook;
    awl_dbus_ui64_t ihook;
    awl_dbus_void_t nhook;
    void* userdata;

    char name[1024];
    UT_hash_handle hh;
} awl_dbus_callback_t;

struct awl_dbus_listener_t {
    awl_dbus_callback_t* callbacks;

    _Atomic int running;

    pthread_t receiver;
    sem_t sem;
};

typedef enum {
    awl_dbus_param_invalid = -1,
    awl_dbus_param_none = 0,
    awl_dbus_param_int,
    awl_dbus_param_str,
} awl_dbus_param_t;

static void* receiver_thread(void* handle_) {
    awl_dbus_listener_t* handle = (awl_dbus_listener_t*)handle_;
    DBusMessage* msg;
    DBusMessageIter args;
    DBusConnection* conn;
    DBusError err;
    int ret;
    char* sigvalue = NULL;
    uint64_t integer = 0;

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
    ret = dbus_bus_request_name(conn, AWL_DBUS_ROOT_PATH, DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
    if (dbus_error_is_set(&err)) {
        awl_err_printf( "dbus Name Error (%s)", err.message);
        dbus_error_free(&err);
    }
    if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != ret) return NULL;

    // add a rule for which messages we want to see
    dbus_bus_add_match(conn, "type='signal',interface='" AWL_DBUS_ROOT_PATH "'", &err); // see signals from the given interface
    dbus_connection_flush(conn);
    if (dbus_error_is_set(&err)) {
        awl_err_printf( "dbus Match Error (%s)", err.message);
        return NULL;
    }
    awl_log_printf("dbus Match rule sent");

    // loop listening for signals being emmitted
    while (atomic_load( &handle->running )) {

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
        sem_wait(&handle->sem);

        awl_dbus_callback_t *s, *tmp;
        HASH_ITER(hh, handle->callbacks, s, tmp) {
            awl_dbus_param_t dtype = awl_dbus_param_invalid;
            if (dbus_message_is_signal(msg, AWL_DBUS_ROOT_PATH, s->name)) {
                // read the parameters
                if (!dbus_message_iter_init(msg, &args)) {
                    dtype = awl_dbus_param_none;
                } else {
                    switch (dbus_message_iter_get_arg_type(&args)) {
                        case DBUS_TYPE_STRING:
                            dtype = awl_dbus_param_str;
                            dbus_message_iter_get_basic(&args, &sigvalue);
                            break;
                        case DBUS_TYPE_BYTE:
                        case DBUS_TYPE_BOOLEAN:
                        case DBUS_TYPE_INT16:
                        case DBUS_TYPE_UINT16:
                        case DBUS_TYPE_INT32:
                        case DBUS_TYPE_UINT32:
                        case DBUS_TYPE_INT64:
                        case DBUS_TYPE_UINT64:
                        case DBUS_TYPE_DOUBLE:
                            dtype = awl_dbus_param_int;
                            dbus_message_iter_get_basic(&args, &integer);
                            break;
                        default:
                            awl_err_printf("dbus: arg is of unsupported type!");
                            break;
                    }
                }

                switch (dtype) {
                    case awl_dbus_param_none:
                        if (s->nhook) {
                            awl_log_printf( "dbus: calling hook %p without arg", s->nhook );
                            s->nhook( s->userdata );
                        } else {
                            awl_log_printf( "dbus: expected parameter for hook %s", s->name );
                        }
                        break;
                    case awl_dbus_param_int:
                        if (s->ihook) {
                            awl_log_printf( "dbus: calling hook %p with arg %lu", s->ihook, integer );
                            s->ihook( integer, s->userdata );
                        } else {
                            awl_log_printf( "dbus: expected something else than an integer for hook %s", s->name );
                        }
                        break;
                    case awl_dbus_param_str:
                        if (s->hook) {
                            awl_log_printf( "dbus: calling hook %p with arg %s", s->hook, sigvalue );
                            s->hook( sigvalue, s->userdata );
                        } else {
                            awl_log_printf( "dbus: expected something else than a string for hook %s", s->name );
                        }
                        break;
                    default:
                    case awl_dbus_param_invalid:
                        awl_err_printf( "dbus: invalid argument, not calling hook" );
                        break;
                }
            }
        }
        sem_post(&handle->sem);

        // free the message
        dbus_message_unref(msg);
    }
    return NULL;
}

awl_dbus_listener_t* awl_dbus_init( void ) {
    awl_dbus_listener_t* h = calloc(1, sizeof(awl_dbus_listener_t));
    atomic_init( &h->running, 1 );
    h->callbacks = NULL;
    sem_init( &h->sem, 0, 1 );
    AWL_PTHREAD_CREATE( &h->receiver, NULL, &receiver_thread, h );
    return h;
}

typedef void (*funcptr_t)( void );

static void awl_dbus_add_callback_any( awl_dbus_listener_t* bus, const char* name_,
        funcptr_t hook, awl_dbus_param_t type, void* userdata ) {
    sem_wait( &bus->sem );

    awl_dbus_callback_t* s = NULL;
    HASH_FIND_STR(bus->callbacks, name_, s);
    if (s) {
        awl_log_printf("dbus callback %s already in use", name_);
        goto end_add_callback;
    }

    s = calloc(1, sizeof(awl_dbus_callback_t));
    switch (type) {
        default:
        case awl_dbus_param_invalid:
            break;
        case awl_dbus_param_int:
            s->ihook = (awl_dbus_ui64_t)hook;
            break;
        case awl_dbus_param_none:
            s->nhook = (awl_dbus_void_t)hook;
            break;
        case awl_dbus_param_str:
            s->hook = (awl_dbus_hook_t)hook;
            break;
    }
    strncpy( s->name, name_, 1023 );
    s->userdata = userdata;

    HASH_ADD_STR( bus->callbacks, name, s );

end_add_callback:
    sem_post( &bus->sem );
}

void awl_dbus_add_callback( awl_dbus_listener_t* bus, const char* name,
        awl_dbus_hook_t hook, void* userdata ) {
    awl_dbus_add_callback_any( bus, name, (funcptr_t)hook, awl_dbus_param_str, userdata );
}
void awl_dbus_add_callback_int( awl_dbus_listener_t* bus, const char* name,
        awl_dbus_ui64_t hook, void* userdata ) {
    awl_dbus_add_callback_any( bus, name, (funcptr_t)hook, awl_dbus_param_int, userdata );
}
void awl_dbus_add_callback_void( awl_dbus_listener_t* bus, const char* name,
        awl_dbus_void_t hook, void* userdata ) {
    awl_dbus_add_callback_any( bus, name, (funcptr_t)hook, awl_dbus_param_none, userdata );
}

void awl_dbus_remove_callback( awl_dbus_listener_t* bus, const char* name_ ) {
    sem_wait( &bus->sem );
    awl_dbus_callback_t* s = NULL;

    HASH_FIND_STR(bus->callbacks, name_, s);
    if (s) {
        HASH_DEL(bus->callbacks, s);
        free(s);
    } else {
        awl_log_printf( "could not find callback %s", name_ );
    }
    sem_post( &bus->sem );
}

void awl_dbus_destroy( awl_dbus_listener_t* bus ) {
    atomic_store( &bus->running, 0 );
    pthread_join( bus->receiver, NULL );

    sem_wait( &bus->sem );
    awl_dbus_callback_t *s, *tmp;
    HASH_ITER(hh, bus->callbacks, s, tmp) {
        HASH_DEL(bus->callbacks, s);
        free(s);
    }
    HASH_CLEAR(hh, bus->callbacks);

    sem_destroy( &bus->sem );
    free(bus);
}

void awl_dbus_notify( const char* name, const char* sigvalue ) {
    awl_log_printf("dbus: sending signal '%s' with value '%s'", name, sigvalue);

    DBusError err;
    dbus_error_init(&err);

    // connect to the DBUS system bus, and check for errors
    DBusConnection* conn;
    if (!(conn = dbus_bus_get(DBUS_BUS_SESSION, &err))) {
        awl_err_printf("dbus: couldn't establish a connection");
        goto notify_error_out;
    }
    if (dbus_error_is_set(&err)) goto notify_error_out;

    // register our name on the bus, and check for errors
    int ret = dbus_bus_request_name(conn, AWL_DBUS_ROOT_PATH, DBUS_NAME_FLAG_REPLACE_EXISTING , &err);
    if (dbus_error_is_set(&err)) goto notify_error_out;
    if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER == ret)
        awl_log_printf( "dbus: we're primary owner? trying to continue anyways…" );

    DBusMessage* msg;
    if (!(msg = dbus_message_new_signal("/", AWL_DBUS_ROOT_PATH, name))) {
        awl_err_printf( "dbus: message == NULL" );
        goto notify_error_out;
    }

    // append arguments
    DBusMessageIter args;
    dbus_message_iter_init_append(msg, &args);
    const char nullsignal[] = ""; if (!sigvalue) sigvalue = nullsignal;
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &sigvalue)) {
        awl_err_printf( "dbus: OOM!" );
        goto notify_error_out;
    }
    // send + flush + free
    dbus_uint32_t serial = 0;
    if (!dbus_connection_send(conn, msg, &serial)) {
        awl_err_printf( "dbus: OOM!" );
        goto notify_error_out;
    }
    dbus_connection_flush(conn);
    dbus_message_unref(msg);

    awl_log_printf( "dbus: sent signal '%s' with value '%s'", name, sigvalue );

notify_error_out:
    if (dbus_error_is_set(&err)) {
        awl_err_printf( "dbus error: %s", err.message );
        dbus_error_free(&err);
    }
}
