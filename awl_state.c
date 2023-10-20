#include "awl_state.h"

static awl_state_t* stateptr = NULL;

awl_state_t** get_awl_state_addr(void) {
    return &stateptr;
}
