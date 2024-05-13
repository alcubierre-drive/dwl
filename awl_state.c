#include "awl_state.h"
#include "awl_util.h"
#include "awl_log.h"
#include "awl_functions.h"
#include "awl_dbus.h"

awl_state_t* awl_state_init( void ) {
    awl_log_printf( "init core awl state" );
    awl_state_t* B = ecalloc(1,sizeof(awl_state_t));
    strcpy( B->broken, "broken" );
    strcpy( B->cursor_image, "left_ptr" );
    B->child_pid = -1;
    B->locked = 0;
    B->layermap = ecalloc(16, sizeof(int));
    B->layermap[B->n_layermap++] = LyrBg;
    B->layermap[B->n_layermap++] = LyrBottom;
    B->layermap[B->n_layermap++] = LyrTop;
    B->layermap[B->n_layermap++] = LyrOverlay;

    // functions
    B->arrange = arrange;
    B->focusclient = focusclient;
    B->focustop = focustop;
    B->printstatus = printstatus;
    B->resize = resize;
    B->dirtomon = dirtomon;
    B->setfloating = setfloating;
    B->setontop = setontop;
    B->xytonode = xytonode;
    B->setmon = setmon;
    B->setfullscreen = setfullscreen;
    B->ipc_send_toggle_vis = ipc_send_toggle_vis;
    B->ipc_send_vis = ipc_send_vis;

    B->awl_is_ready_sem = awl_is_ready_sem;
    B->awl_is_ready = awl_is_ready;
    B->awl_change_modkey = awl_change_modkey;
    B->log = awl_log_printer_;

    B->dbus = awl_dbus_init();
    B->dbus_notify = &awl_dbus_notify;
    B->dbus_add_callback = &awl_dbus_add_callback;
    B->dbus_remove_callback = &awl_dbus_remove_callback;

    B->persistent_plugin_data = ecalloc(1024,1);
    B->persistent_plugin_data_nbytes = 1024;
    return B;
}

void awl_state_free( awl_state_t* B ) {
    awl_log_printf( "free core awl state" );
    free(B->layermap);

    awl_dbus_destroy(B->dbus);
    if (B->persistent_plugin_data)
        free(B->persistent_plugin_data);
    free(B);
}
