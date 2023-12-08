#pragma once

#include <semaphore.h>
#include <time.h>

int sem_timedwait_nano( sem_t* s, float nsec );
