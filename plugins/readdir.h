#pragma once

#include <stdint.h>
#include <semaphore.h>

typedef struct {
    char name[127];
    struct { uint8_t
        isdir:1,
        isbroken:1,
        ishidden:1;
    };
} Filename;

typedef struct {
    Filename files[128];
    int n_files;
    sem_t sem; // caller is responsible for sem_init/sem_destroy
} DesktopFiles;

// returns whether the files have changed
int findfiles( DesktopFiles* wp, const char* path );
