/*
 * ETCH CRANK -- by Synth Centric
 * ETCH DS's storage.c is linked (edit.c calls it) but milestone 1 saves
 * nothing, and the Playdate has no POSIX file layer: newlib's system calls
 * fail cleanly on the device.  Saves will go through the SDK's file API
 * (playdate->file) in a later milestone.  The Simulator uses the host's.
 */
#if TARGET_PLAYDATE
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>
int _open(const char *p, int f, int m) { (void)p; (void)f; (void)m; errno = ENOSYS; return -1; }
int _close(int fd) { (void)fd; return -1; }
int _read(int fd, void *b, size_t n) { (void)fd; (void)b; (void)n; return -1; }
int _write(int fd, const void *b, size_t n) { (void)fd; (void)b; (void)n; return -1; }
int _lseek(int fd, int o, int w) { (void)fd; (void)o; (void)w; return -1; }
int _fstat(int fd, struct stat *s) { (void)fd; s->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { (void)fd; return 0; }
int _getpid(void) { return 1; }
int _kill(int p, int s) { (void)p; (void)s; return -1; }
void _exit(int c) { (void)c; for (;;) { } }
void *_sbrk(ptrdiff_t n) { (void)n; errno = ENOMEM; return (void *)-1; }
#endif
