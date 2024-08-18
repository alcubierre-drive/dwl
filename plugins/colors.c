#include "colors.h"

#define MIN( A, B ) ( (A) < (B) ? (A) : (B) )

typedef struct {
    float r, g, b, a;
} fcolor_t;
typedef union {
    fcolor_t c;
    float f[4];
} fcolor_u;
static fcolor_t pixman2fcolor( pixman_color_t p ) {
    fcolor_u u;
    u.c = (fcolor_t){.r = p.red, .g = p.green, .b = p.blue, .a = p.alpha};
    for (int i=0; i<4; ++i) u.f[i] /= 0xFFFF;
    return u.c;
}
static pixman_color_t fcolor2pixman( fcolor_t c ) {
    pixman_color_t p;
    p.red = MIN(c.r * 0xFFFF, 0xFFFF);
    p.green = MIN(c.g * 0xFFFF, 0xFFFF);
    p.blue = MIN(c.b * 0xFFFF, 0xFFFF);
    p.alpha = MIN(c.a * 0xFFFF, 0xFFFF);
    return p;
}

pixman_color_t alpha_blend_16( pixman_color_t B, pixman_color_t A ) {
    fcolor_u uA, uB, uR = {0};
    uA.c = pixman2fcolor(A);
    uB.c = pixman2fcolor(B);
    uR.c.a = uA.c.a + uB.c.a * (1. - uA.c.a);
    for (int i=0; i<3; ++i)
        uR.f[i] = (uA.f[i] * uA.f[3] + uB.f[i] * uB.f[3] * (1.-uA.f[3])) / uR.f[3];
    return fcolor2pixman(uR.c);
}

pixman_color_t mean_color_16( pixman_color_t A, pixman_color_t B, float wA ) {
    pixman_color_t result = {0};
    result.red = wA * A.red + (1.-wA) * B.red;
    result.green = wA * A.green + (1.-wA) * B.green;
    result.blue = wA * A.blue + (1.-wA) * B.blue;
    result.alpha = wA * A.alpha + (1.-wA) * B.alpha;
    return result;
}

struct awl_colors awl_colors( void ) {
    struct awl_colors c;
    pixman_color_t c16 = {0};
    // tag colors
    c.bg_tags = color_8bit_to_16bit( molokai_dark_gray );
    c16 = color_8bit_to_16bit( molokai_orange );
    c16.alpha = 0x7777;
    c.bg_tags_occ = c16;
    c16 = color_8bit_to_16bit( molokai_purple );
    c16.alpha = 0x7777;
    c.bg_tags_act = c16;
    c16 = color_8bit_to_16bit( molokai_red );
    c16.alpha = 0x7777;
    c.bg_tags_urg = c16;
    c16 = color_8bit_to_16bit( molokai_purple );
    c16.alpha = 0x2222;
    c.fg_tags = alpha_blend_16( white, c16 );

    // status/layout colors
    c.bg_status = c.bg_lay = color_8bit_to_16bit( molokai_light_gray );
    c.fg_status = c.fg_lay = c.fg_tags;

    // window colors
    c16 = color_8bit_to_16bit( molokai_purple );
    c16.alpha = 0x4444;
    c.bg_win = alpha_blend_16( color_8bit_to_16bit(molokai_dark_gray), c16 );
    c16.alpha = 0x9999;
    c.bg_win_act = alpha_blend_16( color_8bit_to_16bit(molokai_dark_gray), c16 );
    c.bg_win_urg = c.bg_tags_occ;
    c.bg_win_min = c.bg_tags;
    c.fg_win = c.fg_tags;

    // widget colors
    c.bg_stats = c.bg_tags;
    c.fg_stats_cpu = color_8bit_to_16bit( molokai_blue );
    c.fg_stats_mem = color_8bit_to_16bit( molokai_orange );
    c.fg_stats_swp = color_8bit_to_16bit( molokai_green );

    return c;
}

