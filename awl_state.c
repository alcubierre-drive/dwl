#include "awl_state.h"

awl_state_t* awl_state_init( void ) {
    awl_state_t* B = calloc(1,sizeof(awl_state_t));
    strcpy( B->broken, "broken" );
    strcpy( B->cursor_image, "left_ptr" );
    B->child_pid = -1;
    B->locked = 0;
    B->layermap = calloc(16, sizeof(int));
    B->layermap[B->n_layermap++] = LyrBg;
    B->layermap[B->n_layermap++] = LyrBottom;
    B->layermap[B->n_layermap++] = LyrTop;
    B->layermap[B->n_layermap++] = LyrOverlay;
    return B;
}

void awl_state_free( awl_state_t* B ) {
    free(B->layermap);
    free(B);
}
