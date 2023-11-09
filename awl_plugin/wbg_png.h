#pragma once

#include <stdio.h>
#include <pixman.h>

pixman_image_t *awl_png_load(FILE *fp, const char *path);
