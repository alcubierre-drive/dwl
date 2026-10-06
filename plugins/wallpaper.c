#define _GNU_SOURCE
#include "wallpaper.h"
#include "readdir.h"
#include "redraw.h"
#include "thread.h"
#include "../wuffs/wuffs.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/random.h>
#include <unistd.h>

#define NFILES (sizeof(((DesktopFiles*)0)->files) / sizeof(Filename))

/* the current one's index in the sorted list; a file, so it survives
 * reloads and restarts */
static const char index_file[] = "/tmp/random_wallpaper.index";
/* larger images are refused rather than allocated (4 bytes per pixel) */
static const uint32_t max_side = 16384;
/* the decoder gets the file this much at a time, and in between a stop
 * request doesn't have to wait for the rest */
static const size_t feed = 1 << 20;
/* how many wallpapers back awl_wallpaper_back() can go */
#define HISTORY 32

/* One per thread; a stuck one is left behind with its thread, never freed. */
typedef struct {
    char dir[PATH_MAX];        /* set before the thread starts, then read-only */
    DesktopFiles files;        /* thread only */
    atomic_int steps, random, back; /* main -> thread: what to do once woken */
    /* the ones shown before, newest last; thread only */
    int history[HISTORY], nhistory;
    /* how many steps and random changes were asked for, and how many of them
     * the newest where includes: equal once all of them are done */
    atomic_uint asked, done;
    /* thread -> main: the newest decoded wallpaper, NULL once taken. Handed
     * over whole, so neither side ever waits for the other. */
    _Atomic(awl_image_t*) pending;
    /* thread -> main: where the list stands (see pack()), 0 until known */
    _Atomic(uint64_t) where;
    awl_thread_t thread;
} Changer;

static Changer* ch;

/* the current index, the list's length and the index random goes to next,
 * in one word so the main thread never sees half an update */
static uint64_t pack( int cur, int n, int rand_next ) {
    return (uint64_t)(uint16_t)n << 32 | (uint64_t)(uint16_t)cur << 16 | (uint16_t)rand_next;
}

/* any of n but cur, from the kernel's random generator */
static int pick( int cur, int n ) {
    if (n < 2) return 0;
    unsigned int r = 0; if (getrandom(&r, sizeof(r), 0) != sizeof(r)) r = 0;
    int i = (int)(r % (unsigned int)(n - 1));
    return i >= cur ? i + 1 : i;
}

static int ispng( const Filename* f ) {
    size_t n = strlen( f->name );
    return !f->isdir && n > 4 && !strcasecmp( f->name + n - 4, ".png" );
}

static int readindex( void ) {
    int i = 0;
    FILE* f = fopen( index_file, "re" );
    if (f) {
        if (fscanf( f, "%d", &i ) != 1) i = 0;
        fclose( f );
    }
    return i;
}

static void writeindex( int i ) {
    /* /tmp is everyone's: don't follow someone else's symlink */
    int fd = open( index_file, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600 );
    if (fd < 0) return;
    dprintf( fd, "%d\n", i );
    close( fd );
}

/* lets the decoder see the next part of the file; 0 if there is none or
 * the thread is to quit */
static int feedmore( Changer* c, wuffs_base__io_buffer* src ) {
    if (src->meta.closed || atomic_load( &c->thread.quit )) return 0;
    size_t left = src->data.len - src->meta.wi;
    src->meta.wi += left < feed ? left : feed;
    src->meta.closed = src->meta.wi == src->data.len;
    return 1;
}

static awl_image_t* decode( Changer* c, const char* path ) {
    awl_image_t* img = NULL;
    void* work = NULL;
    const char* err = NULL;

    int fd = open( path, O_RDONLY | O_CLOEXEC );
    struct stat st;
    if (fd < 0 || fstat( fd, &st ) || st.st_size <= 0) {
        fprintf( stderr, "wallpaper: can't read %s: %s\n", path, strerror( errno ) );
        if (fd >= 0) close( fd );
        return NULL;
    }
    void* map = mmap( NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd );
    if (map == MAP_FAILED) {
        fprintf( stderr, "wallpaper: can't map %s: %s\n", path, strerror( errno ) );
        return NULL;
    }

    wuffs_png__decoder* dec = malloc( sizeof__wuffs_png__decoder() );
    if (!dec) goto out;
    wuffs_base__status s = wuffs_png__decoder__initialize( dec, sizeof__wuffs_png__decoder(),
            WUFFS_VERSION, WUFFS_INITIALIZE__LEAVE_INTERNAL_BUFFERS_UNINITIALIZED );
    if (!wuffs_base__status__is_ok( &s )) { err = wuffs_base__status__message( &s ); goto out; }
    /* the file is ours, a broken one shows as such */
    wuffs_png__decoder__set_quirk_enabled( dec, WUFFS_BASE__QUIRK_IGNORE_CHECKSUM, true );

    wuffs_base__io_buffer src = wuffs_base__ptr_u8__reader( map, (size_t)st.st_size, false );
    src.meta.wi = 0;
    feedmore( c, &src );

    wuffs_base__image_config ic;
    do s = wuffs_png__decoder__decode_image_config( dec, &ic, &src );
    while (s.repr == wuffs_base__suspension__short_read && feedmore( c, &src ));
    if (!wuffs_base__status__is_ok( &s )) { err = wuffs_base__status__message( &s ); goto out; }

    uint32_t w = wuffs_base__pixel_config__width( &ic.pixcfg ),
             h = wuffs_base__pixel_config__height( &ic.pixcfg );
    if (!w || !h || w > max_side || h > max_side) { err = "unsupported size"; goto out; }
    wuffs_base__pixel_config__set( &ic.pixcfg, WUFFS_BASE__PIXEL_FORMAT__BGRA_PREMUL,
                                   WUFFS_BASE__PIXEL_SUBSAMPLING__NONE, w, h );

    size_t len = (size_t)w * h * 4;
    img = malloc( sizeof(*img) + len );
    if (!img) { err = "out of memory"; goto out; }
    img->width = (int)w;
    img->height = (int)h;
    img->stride = (int)w * 4;
    wuffs_base__pixel_buffer pb;
    s = wuffs_base__pixel_buffer__set_from_slice( &pb, &ic.pixcfg,
                                                 wuffs_base__make_slice_u8( img->data, len ) );
    if (!wuffs_base__status__is_ok( &s )) { err = wuffs_base__status__message( &s ); goto out; }

    /* PNG's unfiltering wants the whole inflated image */
    uint64_t wlen = wuffs_png__decoder__workbuf_len( dec ).max_incl;
    work = malloc( wlen ? wlen : 1 );
    if (!work) { err = "out of memory"; goto out; }
    do s = wuffs_png__decoder__decode_frame( dec, &pb, &src, WUFFS_BASE__PIXEL_BLEND__SRC,
                                             wuffs_base__make_slice_u8( work, wlen ), NULL );
    while (s.repr == wuffs_base__suspension__short_read && feedmore( c, &src ));
    if (!wuffs_base__status__is_ok( &s )) err = wuffs_base__status__message( &s );

out:
    if (!dec) err = "out of memory";
    /* a stop request cuts decoding short; that's no error worth a word */
    if (err && !atomic_load( &c->thread.quit ))
        fprintf( stderr, "wallpaper: can't decode %s: %s\n", path, err );
    if (err) {
        free( img );
        img = NULL;
    }
    free( work );
    free( dec );
    munmap( map, (size_t)st.st_size );
    return img;
}

/* steps 0, random 0, back 0: the one the index file names; asked: the
 * requests these include. The ones asked for at once take effect in this
 * order: random or steps, then back. */
static void change( Changer* c, int steps, int random, int back, unsigned asked ) {
    int png[NFILES], n = 0, i;
    findfiles( &c->files, c->dir );
    for (i = 0; i < c->files.n_files; i++)
        if (ispng( &c->files.files[i] )) png[n++] = i;
    if (!n) {
        fprintf( stderr, "wallpaper: no *.png in %s\n", c->dir );
        return;
    }

    int prev = (readindex() % n + n) % n;
    if (!random) {
        i = ((prev + steps) % n + n) % n;
    } else {
        /* the one announced (awl_wallpaper_position()), if the list is
         * still the one it was picked from */
        uint64_t w = atomic_load( &c->where );
        int next = (int)(w & 0xffff);
        i = w && (int)(w >> 32) == n && next < n && next != prev ? next : pick( prev, n );
    }
    if (i != prev) {
        /* full: the oldest goes */
        if (c->nhistory == HISTORY)
            memmove( c->history, c->history + 1, --c->nhistory * sizeof(int) );
        c->history[c->nhistory++] = prev;
    }
    /* nothing left to go back to: stay; the list may have shrunk since */
    for (; back > 0 && c->nhistory; back--)
        i = c->history[--c->nhistory] % n;
    if (i != prev) writeindex( i );
    atomic_store( &c->where, pack( i, n, pick( i, n ) ) );
    atomic_store( &c->done, asked );

    char path[PATH_MAX + sizeof(c->files.files[0].name) + 1];
    snprintf( path, sizeof(path), "%s/%s", c->dir, c->files.files[png[i]].name );
    awl_image_t* img = decode( c, path );
    if (!img) return;
    /* replaces one the main thread hasn't taken yet */
    free( atomic_exchange( &c->pending, img ) );
    awl_redraw_request();
}

static void* changer( void* data ) {
    Changer* c = data;
    struct pollfd fd = { .fd = c->thread.wake_fd, .events = POLLIN };
    int current = 1; /* at first, the one the index file names */

    while (1) {
        /* before the requests themselves: it may include fewer, never more */
        unsigned asked = atomic_load( &c->asked );
        int random = atomic_exchange( &c->random, 0 ),
            steps = atomic_exchange( &c->steps, 0 ),
            back = atomic_exchange( &c->back, 0 );
        if (current || random || steps || back) change( c, steps, random, back, asked );
        current = 0;

        if (poll( &fd, 1, -1 ) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (awl_thread_woken( &c->thread )) break;
    }
    return NULL;
}

void awl_wallpaper_start( const char* dir ) {
    if (!dir) return;
    Changer* n = calloc( 1, sizeof(*n) );
    if (!n) return;
    if (dir[0] == '/') {
        snprintf( n->dir, sizeof(n->dir), "%s", dir );
    } else {
        const char* home = getenv( "HOME" );
        snprintf( n->dir, sizeof(n->dir), "%s/%s", home ? home : "", dir );
    }
    atomic_init( &n->steps, 0 );
    atomic_init( &n->random, 0 );
    atomic_init( &n->back, 0 );
    atomic_init( &n->asked, 0 );
    atomic_init( &n->done, 0 );
    atomic_init( &n->pending, NULL );
    atomic_init( &n->where, 0 );
    if (awl_thread_start( &n->thread, "wallpaper", changer, n )) {
        free( n );
        return;
    }
    ch = n;
}

int awl_wallpaper_stop( void ) {
    if (!ch) return 0;
    Changer* old = ch;
    ch = NULL;
    if (awl_thread_stop( &old->thread, 500 )) {
        fprintf( stderr, "wallpaper: thread stuck (in %s?), not waiting for it\n", old->dir );
        return -1; /* old stays allocated, the thread may still use it */
    }
    free( atomic_load( &old->pending ) );
    free( old );
    return 0;
}

void awl_wallpaper_step( int steps ) {
    if (!ch) return;
    atomic_fetch_add( &ch->steps, steps );
    atomic_fetch_add( &ch->asked, 1 );
    awl_thread_wake( &ch->thread );
}

void awl_wallpaper_random( void ) {
    if (!ch) return;
    atomic_store( &ch->random, 1 );
    atomic_fetch_add( &ch->asked, 1 );
    awl_thread_wake( &ch->thread );
}

void awl_wallpaper_back( void ) {
    if (!ch) return;
    atomic_fetch_add( &ch->back, 1 );
    atomic_fetch_add( &ch->asked, 1 );
    awl_thread_wake( &ch->thread );
}

int awl_wallpaper_position( int* cur, int* n, int* rand_next ) {
    uint64_t w = ch ? atomic_load( &ch->where ) : 0;
    if (!w) return 0;
    *n = (int)(w >> 32);
    *cur = (int)(w >> 16 & 0xffff);
    *rand_next = (int)(w & 0xffff);
    return 1;
}

int awl_wallpaper_settled( int* cur, int* n ) {
    if (!ch || atomic_load( &ch->done ) != atomic_load( &ch->asked )) return 0;
    int rand_next;
    return awl_wallpaper_position( cur, n, &rand_next );
}

awl_image_t* awl_wallpaper_take( void ) {
    return ch ? atomic_exchange( &ch->pending, NULL ) : NULL;
}
