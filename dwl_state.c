#include "dwl.h"

static dwl_state_t* stateptr = NULL;

dwl_state_t** get_dwl_state_addr(void) {
    return &stateptr;
}
