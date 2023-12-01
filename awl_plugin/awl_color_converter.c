#include <stdint.h>

typedef enum {
    AWL_COLOR_FMT_RGB_8 = 0,
    AWL_COLOR_FMT_RGBA_8,
    AWL_COLOR_FMT_ARGB_8,
    AWL_COLOR_FMT_RGB_16,
    AWL_COLOR_FMT_RGBA_16,
    AWL_COLOR_FMT_ARGB_16,
    AWL_COLOR_FMT_BGR_8,
    AWL_COLOR_FMT_BGRA_8,
    AWL_COLOR_FMT_ABGR_8,
    AWL_COLOR_FMT_BGR_16,
    AWL_COLOR_FMT_BGRA_16,
    AWL_COLOR_FMT_ABGR_16,
    AWL_COLOR_FMT_NUM,
} awl_color_fmt_t;

typedef struct {
    union {
        union {
            struct { uint8_t rgb_8[3]; uint8_t _8a; };
            uint32_t rgb_8_x;
        };
        union {
            uint8_t rgba_8[4];
            uint32_t rgba_8_x;
        };
        union {
            uint8_t argb_8[4];
            uint32_t argb_8_x;
        };
        union {
            struct { uint16_t rgb_16[3]; uint8_t _16a; };
            uint64_t rgb_16_x;
        };
        union {
            uint16_t rgba_16[4];
            uint64_t rgba_16_x;
        };
        union {
            uint16_t argb_16[4];
            uint64_t argb_16_x;
        };
        union {
            struct { uint8_t bgr_8[3]; uint8_t _8b; };
            uint32_t bgr_8_x;
        };
        union {
            uint8_t bgra_8[4];
            uint32_t bgra_8_x;
        };
        union {
            uint8_t abgr_8[4];
            uint32_t abgr_8_x;
        };
        union {
            struct { uint16_t bgr_16[3]; uint8_t _16b; };
            uint64_t bgr_16_x;
        };
        union {
            uint16_t bgra_16[4];
            uint64_t bgra_16_x;
        };
        union {
            uint16_t abgr_16[4];
            uint64_t abgr_16_x;
        };
    };
    awl_color_fmt_t type;
} awl_color_t;

awl_color_t awl_color_convert_val( awl_color_t c, awl_color_fmt_t f );
awl_color_t* awl_color_convert_ptr( awl_color_t* c, awl_color_fmt_t f );

static inline uint64_t any_to_u64( const awl_color_t* c ) {
    union {
        uint64_t u64;
        uint16_t rgba[4];
    } result = {.u64 = ~0x0};
    switch (c->type) {
        case AWL_COLOR_FMT_RGB_8:
            result.rgba[0] = c->rgb_8[0] * 256;
            result.rgba[1] = c->rgb_8[1] * 256;
            result.rgba[2] = c->rgb_8[2] * 256;
            break;
        case AWL_COLOR_FMT_RGBA_8:
            result.rgba[0] = c->rgba_8[0] * 256;
            result.rgba[1] = c->rgba_8[1] * 256;
            result.rgba[2] = c->rgba_8[2] * 256;
            result.rgba[3] = c->rgba_8[3] * 256;
            break;
        case AWL_COLOR_FMT_ARGB_8:
            result.rgba[0] = c->argb_8[1] * 256;
            result.rgba[1] = c->argb_8[2] * 256;
            result.rgba[2] = c->argb_8[3] * 256;
            result.rgba[3] = c->argb_8[0] * 256;
            break;
        case AWL_COLOR_FMT_RGB_16:
            result.rgba[0] = c->rgb_16[0];
            result.rgba[1] = c->rgb_16[1];
            result.rgba[2] = c->rgb_16[2];
            break;
        case AWL_COLOR_FMT_RGBA_16:
            result.rgba[0] = c->rgba_16[0];
            result.rgba[1] = c->rgba_16[1];
            result.rgba[2] = c->rgba_16[2];
            result.rgba[3] = c->rgba_16[3];
            break;
        case AWL_COLOR_FMT_ARGB_16:
            result.rgba[0] = c->argb_16[1];
            result.rgba[1] = c->argb_16[2];
            result.rgba[2] = c->argb_16[3];
            result.rgba[3] = c->argb_16[0];
            break;
        case AWL_COLOR_FMT_BGR_8:
            result.rgba[0] = c->bgr_8[2] * 256;
            result.rgba[1] = c->bgr_8[1] * 256;
            result.rgba[2] = c->bgr_8[0] * 256;
            break;
        case AWL_COLOR_FMT_BGRA_8:
            result.rgba[0] = c->bgra_8[2] * 256;
            result.rgba[1] = c->bgra_8[1] * 256;
            result.rgba[2] = c->bgra_8[0] * 256;
            result.rgba[3] = c->bgra_8[3] * 256;
            break;
        case AWL_COLOR_FMT_ABGR_8:
            result.rgba[0] = c->abgr_8[3] * 256;
            result.rgba[1] = c->abgr_8[2] * 256;
            result.rgba[2] = c->abgr_8[1] * 256;
            result.rgba[3] = c->abgr_8[0] * 256;
            break;
        case AWL_COLOR_FMT_BGR_16:
            result.rgba[0] = c->bgr_16[2];
            result.rgba[1] = c->bgr_16[1];
            result.rgba[2] = c->bgr_16[0];
            break;
        case AWL_COLOR_FMT_BGRA_16:
            result.rgba[0] = c->bgra_16[2];
            result.rgba[1] = c->bgra_16[1];
            result.rgba[2] = c->bgra_16[0];
            result.rgba[3] = c->bgra_16[3];
            break;
        case AWL_COLOR_FMT_ABGR_16:
            result.rgba[0] = c->abgr_16[3];
            result.rgba[1] = c->abgr_16[2];
            result.rgba[2] = c->abgr_16[1];
            result.rgba[3] = c->abgr_16[0];
            break;
        default:
            break;
    }
    return result.u64;
}

static inline void u64_to_any( uint64_t c, awl_color_t* out ) {
    union {
        uint64_t u64;
        uint16_t rgba[4];
    } in = {.u64 = c};
    switch (out->type) {
        case AWL_COLOR_FMT_RGB_8:
            out->rgb_8[0] = in.rgba[0] / 256;
            out->rgb_8[1] = in.rgba[1] / 256;
            out->rgb_8[2] = in.rgba[2] / 256;
            break;
        case AWL_COLOR_FMT_RGBA_8:
            out->rgba_8[0] = in.rgba[0] / 256;
            out->rgba_8[1] = in.rgba[1] / 256;
            out->rgba_8[2] = in.rgba[2] / 256;
            out->rgba_8[3] = in.rgba[3] / 256;
            break;
        case AWL_COLOR_FMT_ARGB_8:
            out->rgba_8[1] = in.rgba[0] / 256;
            out->rgba_8[2] = in.rgba[1] / 256;
            out->rgba_8[3] = in.rgba[2] / 256;
            out->rgba_8[0] = in.rgba[3] / 256;
            break;
        case AWL_COLOR_FMT_RGB_16:
            out->rgb_16[0] = in.rgba[0];
            out->rgb_16[1] = in.rgba[1];
            out->rgb_16[2] = in.rgba[2];
            break;
        case AWL_COLOR_FMT_RGBA_16:
            out->rgba_16[0] = in.rgba[0];
            out->rgba_16[1] = in.rgba[1];
            out->rgba_16[2] = in.rgba[2];
            out->rgba_16[3] = in.rgba[3];
            break;
        case AWL_COLOR_FMT_ARGB_16:
            out->rgba_16[1] = in.rgba[0];
            out->rgba_16[2] = in.rgba[1];
            out->rgba_16[3] = in.rgba[2];
            out->rgba_16[0] = in.rgba[3];
            break;
        case AWL_COLOR_FMT_BGR_8:
            out->bgr_8[0] = in.rgba[2] / 256;
            out->bgr_8[1] = in.rgba[1] / 256;
            out->bgr_8[2] = in.rgba[0] / 256;
            break;
        case AWL_COLOR_FMT_BGRA_8:
            out->bgra_8[0] = in.rgba[2] / 256;
            out->bgra_8[1] = in.rgba[1] / 256;
            out->bgra_8[2] = in.rgba[0] / 256;
            out->bgra_8[3] = in.rgba[3] / 256;
            break;
        case AWL_COLOR_FMT_ABGR_8:
            out->abgr_8[0] = in.rgba[3] / 256;
            out->abgr_8[1] = in.rgba[2] / 256;
            out->abgr_8[2] = in.rgba[1] / 256;
            out->abgr_8[3] = in.rgba[0] / 256;
            break;
        case AWL_COLOR_FMT_BGR_16:
            out->bgr_16[0] = in.rgba[2];
            out->bgr_16[1] = in.rgba[1];
            out->bgr_16[2] = in.rgba[0];
            break;
        case AWL_COLOR_FMT_BGRA_16:
            out->bgra_16[0] = in.rgba[2];
            out->bgra_16[1] = in.rgba[1];
            out->bgra_16[2] = in.rgba[0];
            out->bgra_16[3] = in.rgba[3];
            break;
        case AWL_COLOR_FMT_ABGR_16:
            out->abgr_16[0] = in.rgba[3];
            out->abgr_16[1] = in.rgba[2];
            out->abgr_16[2] = in.rgba[1];
            out->abgr_16[3] = in.rgba[0];
            break;
        default:
            break;
    }
}

awl_color_t* awl_color_convert_ptr( awl_color_t* c, awl_color_fmt_t f ) {
    if (c->type == f) return c;
    uint64_t u = any_to_u64( c );
    c->type = f;
    u64_to_any( u, c );
    return c;
}
awl_color_t awl_color_convert_val( awl_color_t c, awl_color_fmt_t f ) {
    awl_color_convert_ptr( &c, f );
    return c;
}

/* #include <stdio.h> */
/* int main() { */
/*     awl_color_t c = {.rgb_8_x = 0xeeddcc, .type = AWL_COLOR_FMT_BGR_8}; */
/*     awl_color_convert_ptr( &c, AWL_COLOR_FMT_RGBA_8 ); */
/*     printf( "%i %i %i\n", c.rgba_8[0], c.rgba_8[1], c.rgba_8[2] ); */
/* } */
