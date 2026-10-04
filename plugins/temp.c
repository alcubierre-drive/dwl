#include "temp.h"
#include <string.h>
#include <stdio.h>
#include <dirent.h>

static int read_line( const char* path, char* buf, size_t n ) {
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    int ok = fgets(buf, n, f) != NULL;
    fclose(f);
    if (ok) buf[strcspn(buf, "\n")] = 0;
    return ok;
}

int temp_find_hwmon( const char* name, const char* label, char* out, size_t n ) {
    // hwmon numbering is not stable across boots, so match by name and label
    DIR* d = opendir("/sys/class/hwmon");
    if (!d) return 0;
    int found = 0;
    struct dirent* e;
    char path[512], buf[64];
    while (!found && (e = readdir(d))) {
        if (strncmp(e->d_name, "hwmon", 5)) continue;
        snprintf(path, sizeof(path), "/sys/class/hwmon/%s/name", e->d_name);
        if (!read_line(path, buf, sizeof(buf)) || strcmp(buf, name)) continue;
        for (int i=1; i<64 && !found; ++i) {
            snprintf(path, sizeof(path), "/sys/class/hwmon/%s/temp%d_label", e->d_name, i);
            if (!read_line(path, buf, sizeof(buf)) || strncmp(buf, label, strlen(label))) continue;
            found = snprintf(out, n, "/sys/class/hwmon/%s/temp%d_input", e->d_name, i) < (int)n;
        }
    }
    closedir(d);
    return found;
}

void temp_init( awl_temperature_t* temp ) {
    sem_init( &temp->sem, 0, 1 );
}

void temp_fini( awl_temperature_t* temp ) {
    sem_destroy( &temp->sem );
}

int temp_update( awl_temperature_t* temp ) {
    // read outside the lock, the bar only try-locks
    float temps[sizeof(temp->temps)/sizeof(temp->temps[0])] = {0};
    uint8_t idx[sizeof(temp->idx)/sizeof(temp->idx[0])] = {0};
    uint8_t ntemps = 0;
    for (int i=0; i<temp->f_ntemps; ++i) {
        FILE* f = fopen(temp->f_files[i], "r");
        if (f) {
            long u = 0;
            if (fscanf(f, "%li", &u) == 1) {
                temps[ntemps] = (float)u/(float)1000.0;
                idx[ntemps++] = i;
            }
            fclose(f);
        }
    }

    sem_wait( &temp->sem );
    int changed = temp->ntemps != ntemps ||
        memcmp( temp->temps, temps, ntemps * sizeof(temps[0]) );
    memcpy( temp->temps, temps, sizeof(temps) );
    memcpy( temp->idx, idx, sizeof(idx) );
    temp->ntemps = ntemps;
    sem_post( &temp->sem );
    return changed;
}

static uint32_t colormap[128] = {
    0x40cfebff, 0x43cee8ff, 0x46cee5ff, 0x49cde2ff, 0x4cccdfff, 0x4fcbdcff,
    0x52cad9ff, 0x55cad6ff, 0x58c9d3ff, 0x5bc8d0ff, 0x5ec7cdff, 0x61c7caff,
    0x64c6c7ff, 0x67c5c5ff, 0x6ac4c2ff, 0x6cc4bfff, 0x6fc3bcff, 0x72c2b9ff,
    0x75c1b6ff, 0x78c0b3ff, 0x7bc0b0ff, 0x7ebfadff, 0x81beaaff, 0x84bda7ff,
    0x87bda4ff, 0x8abca1ff, 0x8dbb9eff, 0x90ba9bff, 0x93ba98ff, 0x96b995ff,
    0x99b892ff, 0x9cb78fff, 0x9fb68cff, 0xa2b689ff, 0xa5b586ff, 0xa8b483ff,
    0xabb380ff, 0xaeb37dff, 0xb1b27aff, 0xb4b177ff, 0xb7b074ff, 0xbab071ff,
    0xbcaf6eff, 0xbfae6bff, 0xc2ad69ff, 0xc5ac66ff, 0xc8ac63ff, 0xcbab60ff,
    0xceaa5dff, 0xd1a95aff, 0xd4a957ff, 0xd7a854ff, 0xdaa751ff, 0xdda64eff,
    0xe0a64bff, 0xe3a548ff, 0xe6a445ff, 0xe9a342ff, 0xeca33fff, 0xefa23cff,
    0xf2a139ff, 0xf5a036ff, 0xf89f33ff, 0xfb9f30ff, 0xfd9d2fff, 0xfd9b30ff,
    0xfd9931ff, 0xfd9832ff, 0xfd9634ff, 0xfd9435ff, 0xfd9336ff, 0xfc9137ff,
    0xfc8f38ff, 0xfc8e3aff, 0xfc8c3bff, 0xfc8a3cff, 0xfc883dff, 0xfc873eff,
    0xfc853fff, 0xfc8341ff, 0xfc8242ff, 0xfc8043ff, 0xfc7e44ff, 0xfc7d45ff,
    0xfc7b47ff, 0xfc7948ff, 0xfc7849ff, 0xfb764aff, 0xfb744bff, 0xfb724dff,
    0xfb714eff, 0xfb6f4fff, 0xfb6d50ff, 0xfb6c51ff, 0xfb6a53ff, 0xfb6854ff,
    0xfb6755ff, 0xfb6556ff, 0xfb6357ff, 0xfb6159ff, 0xfb605aff, 0xfb5e5bff,
    0xfa5c5cff, 0xfa5b5dff, 0xfa595eff, 0xfa5760ff, 0xfa5661ff, 0xfa5462ff,
    0xfa5263ff, 0xfa5164ff, 0xfa4f66ff, 0xfa4d67ff, 0xfa4b68ff, 0xfa4a69ff,
    0xfa486aff, 0xfa466cff, 0xfa456dff, 0xfa436eff, 0xf9416fff, 0xf94070ff,
    0xf93e72ff, 0xf93c73ff, 0xf93a74ff, 0xf93975ff, 0xf93776ff, 0xf93578ff,
    0xf93479ff, 0xf9327aff };

uint32_t temp_color( float T, float min, float max ) {
    float t = (T-min)/(max-min);
    int ncolors = sizeof(colormap)/sizeof(colormap[0]);
    int idx = t * ncolors;
    if (idx < 0) idx = 0;
    if (idx >= ncolors) idx = ncolors-1;
    return colormap[idx];
}
