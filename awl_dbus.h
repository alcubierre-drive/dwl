#pragma once

#include <stdint.h>

#define AWL_DBUS_ROOT_PATH "org.freedesktop.awl"

typedef void (*awl_dbus_hook_t)( const char* signal, void* userdata );
typedef void (*awl_dbus_ui64_t)( uint64_t signal, void* userdata );
typedef void (*awl_dbus_void_t)( void* userdata );

typedef struct awl_dbus_listener_t awl_dbus_listener_t;
awl_dbus_listener_t* awl_dbus_init( void );
void awl_dbus_destroy( awl_dbus_listener_t* bus );

void awl_dbus_add_callback( awl_dbus_listener_t* bus, const char* name,
        awl_dbus_hook_t hook, void* userdata );
void awl_dbus_add_callback_int( awl_dbus_listener_t* bus, const char* name,
        awl_dbus_ui64_t hook, void* userdata );
void awl_dbus_add_callback_void( awl_dbus_listener_t* bus, const char* name,
        awl_dbus_void_t hook, void* userdata );
void awl_dbus_remove_callback( awl_dbus_listener_t* bus, const char* name );
void awl_dbus_notify( const char* name, const char* signal );
