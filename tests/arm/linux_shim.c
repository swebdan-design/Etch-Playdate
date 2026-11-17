/*
 * ETCH CRANK -- by Synth Centric
 * A tiny Linux user-mode runtime for code built with the Playdate's compiler
 * flags (Cortex-M7, Thumb-2, FPv5 hard-float), so it can run under qemu-arm
 * on the host: _start, newlib's system calls onto Linux syscalls, a heap.
 * Used only by tools/arm_parity.sh and tools/arm_cost.sh, never in a build
 * of the game.
 */
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <sys/stat.h>

int main(int argc, char **argv);
void __libc_init_array(void);
void _init(void) {}
void _fini(void) {}

static long sys3(long nr, long a, long b, long c)
{
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    register long r7 __asm__("r7") = nr;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
    return r0;
}

void _exit(int code) { for (;;) sys3(248, code, 0, 0); }

void _cstart(long *sp)
{
    int argc = (int)sp[0];
    char **argv = (char **)(sp + 1);
    __libc_init_array();
    _exit(main(argc, argv));
}

__attribute__((naked)) void _start(void)
{
    __asm__ volatile("mov r0, sp\n\tbl _cstart\n");
}

int _write(int fd, const void *b, size_t n) { return (int)sys3(4, fd, (long)b, (long)n); }
int _read(int fd, void *b, size_t n) { return (int)sys3(3, fd, (long)b, (long)n); }
int _close(int fd) { return (int)sys3(6, fd, 0, 0); }
int _lseek(int fd, int o, int w) { return (int)sys3(19, fd, o, w); }
int _isatty(int fd) { return fd <= 2; }
int _fstat(int fd, struct stat *s) { (void)fd; s->st_mode = S_IFCHR; return 0; }
int _getpid(void) { return 1; }
int _kill(int p, int s) { (void)p; (void)s; return -1; }
int _open(const char *p, int f, int m)
{
    int lf = f & 3;                    /* newlib's flags are not Linux's */
    if (f & 0x0008) lf |= 0x400;       /* O_APPEND */
    if (f & 0x0200) lf |= 0x40;        /* O_CREAT  */
    if (f & 0x0400) lf |= 0x200;       /* O_TRUNC  */
    if (f & 0x0800) lf |= 0x80;        /* O_EXCL   */
    return (int)sys3(5, (long)p, lf, m ? m : 0644);
}
static char heap[64u << 20] __attribute__((aligned(8)));
static size_t heap_used;
void *_sbrk(ptrdiff_t n)
{
    void *p;
    if (heap_used + (size_t)n > sizeof heap) { errno = ENOMEM; return (void *)-1; }
    p = heap + heap_used;
    heap_used += (size_t)n;
    return p;
}
