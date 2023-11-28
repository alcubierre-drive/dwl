#include "../awl_pthread.h"
#include "wbg_png.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include <png.h>
#include <pixman.h>

#include "init.h"
#include "../awl_log.h"

static inline int stride_for_format_and_width(pixman_format_code_t format, int width) {
    return (((PIXMAN_FORMAT_BPP(format) * width + 7) / 8 + 4 - 1) & -4);
}

static void data_free( pixman_image_t* pix, void* data ) {
    (void)pix;
    free(data);
}

pixman_image_t * awl_png_load(FILE *fp, const char *path) {
    pixman_image_t *pix = NULL;

    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_bytepp row_pointers = NULL;
    uint8_t *image_data = NULL;

    if (fseek(fp, 0, SEEK_SET) < 0) {
        P_awl_err_printf("%s: failed to seek to beginning of file (%s)", path, strerror(errno));
        return NULL;
    }

    /* Verify PNG header */
    uint8_t header[8] = {0};
    if (fread(header, 1, 8, fp) != 8 || png_sig_cmp(header, 0, 8)) {
        goto err;
    }

    /* Prepare for reading the PNG */
    if ((png_ptr = png_create_read_struct(
             PNG_LIBPNG_VER_STRING, NULL, NULL, NULL)) == NULL ||
        (info_ptr = png_create_info_struct(png_ptr)) == NULL)
    {
        P_awl_err_printf("%s: failed to initialize libpng", path);
        goto err;
    }

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, 8);

    /* Get meta data */
    png_read_info(png_ptr, info_ptr);
    int width = png_get_image_width(png_ptr, info_ptr);
    int height = png_get_image_height(png_ptr, info_ptr);
    png_byte color_type = png_get_color_type(png_ptr, info_ptr);
    png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    int channels = png_get_channels(png_ptr, info_ptr);

    P_awl_vrb_printf("%s: %dx%d@%hhubpp, %d channels", path, width, height, bit_depth, channels);

    png_set_packing(png_ptr);
    png_set_interlace_handling(png_ptr);
    png_set_strip_16(png_ptr);  /* "pack" 16-bit colors to 8-bit */
    png_set_bgr(png_ptr);

    /* pixman expects pre-multiplied alpha */
    //png_set_alpha_mode(png_ptr, PNG_ALPHA_PREMULTIPLIED, 2.2);

    /* Tell libpng to expand to RGB(A) when necessary, and tell pixman
     * whether we have alpha or not */
    pixman_format_code_t format = {0};
    switch (color_type) {
    case PNG_COLOR_TYPE_GRAY:
    case PNG_COLOR_TYPE_GRAY_ALPHA:
        P_awl_vrb_printf("%d-bit gray%s", bit_depth,
                color_type == PNG_COLOR_TYPE_GRAY_ALPHA ? "+alpha" : "");

        if (bit_depth < 8)
            png_set_expand_gray_1_2_4_to_8(png_ptr);

        png_set_gray_to_rgb(png_ptr);
        format = color_type == PNG_COLOR_TYPE_GRAY ? PIXMAN_r8g8b8 : PIXMAN_x8r8g8b8;
        break;

    case PNG_COLOR_TYPE_PALETTE:
        P_awl_vrb_printf("%d-bit colormap%s", bit_depth,
                png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS) ? "+tRNS" : "");

        png_set_palette_to_rgb(png_ptr);
        if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
            png_set_tRNS_to_alpha(png_ptr);
            format = PIXMAN_x8r8g8b8;
        } else
            format = PIXMAN_r8g8b8;
        break;

    case PNG_COLOR_TYPE_RGB:
        P_awl_vrb_printf("RGB");
        format = PIXMAN_r8g8b8;
        break;

    case PNG_COLOR_TYPE_RGBA:
        P_awl_vrb_printf("RGBA");
        format = PIXMAN_x8r8g8b8;
        break;
    }

    png_read_update_info(png_ptr, info_ptr);

    size_t row_bytes __attribute__((unused)) = png_get_rowbytes(png_ptr, info_ptr);
    int stride = stride_for_format_and_width(format, width);
    image_data = malloc(height * stride);

    P_awl_vrb_printf("stride=%d, row-bytes=%zu", stride, row_bytes);
    assert(stride >= (int)row_bytes);

    row_pointers = malloc(height * sizeof(png_bytep));
    for (int i = 0; i < height; i++)
        row_pointers[i] = &image_data[i * stride];

    png_read_image(png_ptr, row_pointers);

    pix = pixman_image_create_bits_no_clear(
        format, width, height, (uint32_t *)image_data, stride);

    pixman_image_set_destroy_function(pix, data_free, image_data);

err:
    if (pix == NULL)
        free(image_data);
    free(row_pointers);
    if (png_ptr != NULL)
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    return pix;
}
