#include "sem_time.h"
#include <math.h>

int sem_timedwait_nano( sem_t* s, float nsec ) {
    struct timespec ts = {0};
    /* sem_timedwait() interprets abstime against CLOCK_REALTIME */
    clock_gettime( CLOCK_REALTIME, &ts );
    ts.tv_nsec += lroundf(nsec);
    ts.tv_sec += (ts.tv_nsec / 1000000000);
    ts.tv_nsec %= 1000000000;
    return sem_timedwait( s, &ts );
}
