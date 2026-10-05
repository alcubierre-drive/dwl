/* The reloadable half of config.h, built into libawlplugins.so (see
 * config.h and awl_config_t in awl_plugin_abi.h). The functions config.h
 * binds to keys and layouts are dwl's; here they are same-named wrappers
 * calling them through the host table. */
#include "dwl.h"
#include "awl_plugin_abi.h"
#include "plugins.h"
#include "plugins/colors.h"

#define AWL_ACTION_WRAPPER( name ) \
    __attribute__((unused)) static void name( const Arg* arg ) { awl_host->actions->name( arg ); }
AWL_ACTIONS( AWL_ACTION_WRAPPER )
#undef AWL_ACTION_WRAPPER

#define AWL_ARRANGE_WRAPPER( name ) \
    __attribute__((unused)) static void name( Monitor* m ) { awl_host->arranges->name( m ); }
AWL_ARRANGES( AWL_ARRANGE_WRAPPER )
#undef AWL_ARRANGE_WRAPPER

#define AWL_CONFIG_RELOADABLE_ONLY
#include "config.h"

const awl_config_t* awl_config( void ) {
    static awl_config_t c;
    c = AWL_CONFIG_TABLE;
    return &c;
}
