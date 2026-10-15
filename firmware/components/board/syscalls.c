// newlib I/O hooks of the app (the bootloader uses CubeMX's syscalls.c and sysmem.c). There is no
// file I/O, so these fail cleanly instead of linking nosys.specs' warning stubs. _sbrk always fails
// (spec §5.5): no allocation can escape the heap that Plan 05 routes malloc to.
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

int _close(int fd)
{
    (void)fd;
    errno = ENOSYS;
    return -1;
}

off_t _lseek(int fd, off_t off, int whence)
{
    (void)fd;
    (void)off;
    (void)whence;
    errno = ENOSYS;
    return -1;
}

int _read(int fd, void *buf, size_t len)
{
    (void)fd;
    (void)buf;
    (void)len;
    errno = ENOSYS;
    return -1;
}

int _write(int fd, const void *buf, size_t len)
{
    (void)fd;
    (void)buf;
    (void)len;
    errno = ENOSYS;
    return -1;
}

void *_sbrk(ptrdiff_t incr)
{
    (void)incr;
    errno = ENOMEM;
    return (void *)-1;
}

struct stat;

int _fstat(int fd, struct stat *st)
{
    (void)fd;
    (void)st;
    errno = ENOSYS;
    return -1;
}

int _isatty(int fd)
{
    (void)fd;
    errno = ENOSYS;
    return 0;
}

int _getpid(void) { return 1; }

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = ENOSYS;
    return -1;
}
