/*
 * tools/lp64/w2c_host/wasi_min.c -- the WASI calls wasi-libc makes, answered
 * natively for a wasm2c module (see wasi_min.h for why this exists).
 *
 * Every pointer argument is an offset into the module's linear memory; it is
 * bounds-checked here before it is touched, because these are the only
 * accesses to that memory the translated code does not check itself.
 */

#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "host_wasi.h"

typedef struct w2c_wasi__snapshot__preview1 Wasi;

#define WASI_ESUCCESS 0u
#define WASI_EBADF    8u
#define WASI_EFAULT  21u
#define WASI_EINVAL  28u
#define WASI_ENOSYS  52u

void eidos_wasi_init(Wasi *w, wasm_rt_memory_t *mem, int argc, char **argv)
{
    unsigned i;
    w->mem = mem;
    w->argc = argc;
    w->argv = argv;
    w->main_thread = pthread_self();
    for (i = 0; i < 256; ++i) w->fds[i] = -1;
    for (i = 0; i < 3; ++i) w->fds[i] = dup((int)i);
    w->fds[3] = open("/", O_RDONLY | O_DIRECTORY);
    w->fds[4] = open(".", O_RDONLY | O_DIRECTORY);
    if (getenv("EIDOS_WASI_TRACE")) fprintf(stderr, "wasi preopens /=%d .=%d\n", w->fds[3], w->fds[4]);
}

void eidos_wasi_destroy(Wasi *w)
{
    unsigned i;
    for (i = 0; i < 256; ++i) if (w->fds[i] >= 0) close(w->fds[i]);
}

static uint32_t error_code(void)
{
    switch (errno) {
    case EACCES: return 2;
    case EAGAIN: return 6;
    case EBADF: return 8;
    case EEXIST: return 20;
    case EFAULT: return 21;
    case EINTR: return 27;
    case EINVAL: return 28;
    case EIO: return 29;
    case EISDIR: return 31;
    case EMFILE: return 33;
    case ENAMETOOLONG: return 37;
    case ENOENT: return 44;
    case ENOMEM: return 48;
    case ENOSPC: return 51;
    case ENOTDIR: return 54;
    case ENOTEMPTY: return 55;
    case ENOTSUP: return 58;
    case EOVERFLOW: return 61;
    case EPERM: return 63;
    case EPIPE: return 64;
    default: return 29;
    }
}

static int descriptor(Wasi *w, uint32_t fd)
{
    if (fd >= 256 || w->fds[fd] < 0) { errno = EBADF; return -1; }
    return w->fds[fd];
}

static uint8_t *at(Wasi *w, uint32_t off, uint32_t len)
{
    if ((uint64_t)off + len > w->mem->size) {
        return NULL;
    }
    return w->mem->data + off;
}

static uint32_t rd32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static void wr32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static void wr64(uint8_t *p, uint64_t v) { memcpy(p, &v, 8); }

uint32_t w2c_wasi__snapshot__preview1_fd_write(Wasi *w, uint32_t fd, uint32_t iovs,
                                               uint32_t n, uint32_t nwritten)
{
    int f = descriptor(w, fd);
    uint32_t i, total = 0;
    uint8_t *vec = n <= UINT32_MAX / 8u ? at(w, iovs, n * 8u) : NULL;
    uint8_t *out = at(w, nwritten, 4);

    if (f < 0) {
        return WASI_EBADF;
    }
    if (vec == NULL || out == NULL) {
        return WASI_EFAULT;
    }
    for (i = 0; i < n; i++) {
        uint32_t base = rd32(vec + 8u * i), len = rd32(vec + 8u * i + 4u);
        uint8_t *p = at(w, base, len);
        if (p == NULL) {
            return WASI_EFAULT;
        }
        ssize_t got = write(f, p, len);
        if (got < 0) { if (!total) return error_code(); break; }
        total += (uint32_t)got;
        if ((uint32_t)got < len) break;
    }
    wr32(out, total);
    return WASI_ESUCCESS;
}

uint32_t w2c_wasi__snapshot__preview1_fd_close(Wasi *w, uint32_t fd)
{
    int f = descriptor(w, fd);
    if (f < 0) return WASI_EBADF;
    w->fds[fd] = -1;
    if (close(f)) return error_code();
    return WASI_ESUCCESS;
}

uint32_t w2c_wasi__snapshot__preview1_fd_seek(Wasi *w, uint32_t fd, uint64_t off,
                                              uint32_t whence, uint32_t out)
{
    uint8_t *p = at(w, out, 8);
    off_t pos;
    int f = descriptor(w, fd);
    if (f < 0) return WASI_EBADF;
    if (!p) return WASI_EFAULT;
    if (whence > 2) return WASI_EINVAL;
    pos = lseek(f, (int64_t)off, whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END);
    if (pos < 0) return error_code();
    wr64(p, (uint64_t)pos);
    return 0;
}

/* No preopened directories: wasi-libc's start-up asks from fd 3 up and stops
 * at the first EBADF. */
uint32_t w2c_wasi__snapshot__preview1_fd_prestat_get(Wasi *w, uint32_t fd, uint32_t out)
{
    uint8_t *p = at(w, out, 8);
    if (fd != 3) return WASI_EBADF;
    if (descriptor(w, fd) < 0) return WASI_EBADF;
    if (!p) return WASI_EFAULT;
    memset(p, 0, 8);
    wr32(p + 4, 1);
    return 0;
}

uint32_t w2c_wasi__snapshot__preview1_fd_prestat_dir_name(Wasi *w, uint32_t fd,
                                                          uint32_t path, uint32_t len)
{
    uint8_t *p = at(w, path, len);
    if (fd != 3) return WASI_EBADF;
    if (!p) return WASI_EFAULT;
    if (len < 1) return WASI_EINVAL;
    *p = '/';
    return 0;
}

uint32_t w2c_wasi__snapshot__preview1_fd_fdstat_get(Wasi *w, uint32_t fd, uint32_t out)
{
    uint8_t *p = at(w, out, 24);
    if (p == NULL) {
        return WASI_EFAULT;
    }
    int f = descriptor(w, fd);
    struct stat st;
    if (f < 0) {
        return WASI_EBADF;
    }
    memset(p, 0, 24);
    if (fstat(f, &st)) return error_code();
    p[0] = S_ISDIR(st.st_mode) ? 3 : S_ISREG(st.st_mode) ? 4 : 2;
    wr64(p + 8, UINT64_MAX);
    wr64(p + 16, UINT64_MAX);
    return WASI_ESUCCESS;
}

static uint64_t now_ns(uint32_t id)
{
#ifdef _WIN32
    LARGE_INTEGER f, c;
    (void)id;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart * 1e9 / (double)f.QuadPart);
#else
    struct timespec ts;
    clock_gettime(id == 0 ? CLOCK_REALTIME : CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
#endif
}

uint32_t w2c_wasi__snapshot__preview1_clock_time_get(Wasi *w, uint32_t id,
                                                     uint64_t precision, uint32_t out)
{
    uint8_t *p = at(w, out, 8);
    (void)precision;
    if (p == NULL) {
        return WASI_EFAULT;
    }
    if (id > 3) {
        return WASI_EINVAL;
    }
    wr64(p, now_ns(id));
    return WASI_ESUCCESS;
}

uint32_t w2c_wasi__snapshot__preview1_args_sizes_get(Wasi *w, uint32_t argc_out,
                                                     uint32_t size_out)
{
    uint8_t *a = at(w, argc_out, 4), *s = at(w, size_out, 4);
    uint32_t i, size = 0;
    if (a == NULL || s == NULL) {
        return WASI_EFAULT;
    }
    for (i = 0; i < (uint32_t)w->argc; i++) {
        size += (uint32_t)strlen(w->argv[i]) + 1u;
    }
    wr32(a, (uint32_t)w->argc);
    wr32(s, size);
    return WASI_ESUCCESS;
}

uint32_t w2c_wasi__snapshot__preview1_args_get(Wasi *w, uint32_t argv_out, uint32_t buf)
{
    uint32_t i, pos = buf;
    for (i = 0; i < (uint32_t)w->argc; i++) {
        uint32_t len = (uint32_t)strlen(w->argv[i]) + 1u;
        uint8_t *slot = at(w, argv_out + 4u * i, 4), *dst = at(w, pos, len);
        if (slot == NULL || dst == NULL) {
            return WASI_EFAULT;
        }
        wr32(slot, pos);
        memcpy(dst, w->argv[i], len);
        pos += len;
    }
    return WASI_ESUCCESS;
}

uint32_t w2c_wasi__snapshot__preview1_environ_sizes_get(Wasi *w, uint32_t n, uint32_t s)
{
    uint8_t *a = at(w, n, 4), *b = at(w, s, 4);
    if (a == NULL || b == NULL) {
        return WASI_EFAULT;
    }
    wr32(a, 0);
    wr32(b, 0);
    return WASI_ESUCCESS;
}

uint32_t w2c_wasi__snapshot__preview1_environ_get(Wasi *w, uint32_t e, uint32_t b)
{
    (void)w; (void)e; (void)b;
    return WASI_ESUCCESS;
}

void w2c_wasi__snapshot__preview1_proc_exit(Wasi *w, uint32_t code)
{
    if (!pthread_equal(w->main_thread, pthread_self())) exit((int)code);
    w->exit_code = (int)code;
    longjmp(w->exit_to, 1);
}

uint32_t w2c_wasi__snapshot__preview1_fd_read(Wasi *w, uint32_t fd, uint32_t iovs,
                                           uint32_t n, uint32_t nread)
{
    int f = descriptor(w, fd);
    uint8_t *vec = n <= UINT32_MAX / 8u ? at(w, iovs, n * 8u) : NULL;
    uint8_t *out = at(w, nread, 4);
    uint32_t i, total = 0;
    if (f < 0) return WASI_EBADF;
    if (!vec || !out) return WASI_EFAULT;
    for (i = 0; i < n; ++i) {
        uint32_t base = rd32(vec + 8u * i), len = rd32(vec + 8u * i + 4u);
        uint8_t *p = at(w, base, len);
        ssize_t got;
        if (!p) return WASI_EFAULT;
        got = read(f, p, len);
        if (got < 0) { if (!total) return error_code(); break; }
        total += (uint32_t)got;
        if ((uint32_t)got < len) break;
    }
    wr32(out, total);
    return 0;
}

uint32_t w2c_wasi__snapshot__preview1_fd_sync(Wasi *w, uint32_t fd)
{
    int f = descriptor(w, fd);
    if (f < 0) return WASI_EBADF;
    return fsync(f) ? error_code() : 0;
}

uint32_t w2c_wasi__snapshot__preview1_fd_fdstat_set_flags(Wasi *w, uint32_t fd, uint32_t flags)
{
    int f = descriptor(w, fd), old;
    if (f < 0) return WASI_EBADF;
    old = fcntl(f, F_GETFL);
    if (old < 0) return error_code();
    old &= ~(O_APPEND | O_NONBLOCK);
    if (flags & 1u) old |= O_APPEND;
    if (flags & 4u) old |= O_NONBLOCK;
    return fcntl(f, F_SETFL, old) < 0 ? error_code() : 0;
}

uint32_t w2c_wasi__snapshot__preview1_fd_renumber(Wasi *w, uint32_t from, uint32_t to)
{
    int f = descriptor(w, from);
    if (f < 0 || to >= 256) return WASI_EBADF;
    if (from == to) return 0;
    if (w->fds[to] >= 0) close(w->fds[to]);
    w->fds[to] = f;
    w->fds[from] = -1;
    return 0;
}

uint32_t w2c_wasi__snapshot__preview1_fd_readdir(Wasi *w, uint32_t fd,
                                                 uint32_t buf, uint32_t buf_len,
                                                 uint64_t cookie, uint32_t bufused_out)
{
    int f = descriptor(w, fd);
    if (f < 0) return WASI_EBADF;
    uint8_t *b = at(w, buf, buf_len);
    uint8_t *used_ptr = at(w, bufused_out, 4);
    if ((buf_len > 0 && !b) || !used_ptr) return WASI_EFAULT;

    int dfd = dup(f);
    if (dfd < 0) return error_code();
    DIR *dir = fdopendir(dfd);
    if (!dir) {
        close(dfd);
        return error_code();
    }

    uint64_t idx = 0;
    struct dirent *e;
    while (idx < cookie && (e = readdir(dir)) != NULL) {
        ++idx;
    }

    uint32_t used = 0;
    while ((e = readdir(dir)) != NULL) {
        ++idx;
        uint32_t namlen = (uint32_t)strlen(e->d_name);
        if (used + 24 > buf_len) {
            break;
        }
        uint8_t *dst = b + used;
        memset(dst, 0, 24);
        wr64(dst, idx);
        wr64(dst + 8, (uint64_t)e->d_ino);
        wr32(dst + 16, namlen);
        uint8_t wtype = 0;
#ifdef DT_DIR
        if (e->d_type == DT_DIR) wtype = 3;
        else if (e->d_type == DT_REG) wtype = 4;
        else if (e->d_type == DT_LNK) wtype = 7;
#endif
        dst[20] = wtype;
        used += 24;
        uint32_t name_avail = buf_len - used;
        uint32_t name_copy = namlen < name_avail ? namlen : name_avail;
        memcpy(b + used, e->d_name, name_copy);
        used += name_copy;
        if (name_copy < namlen) {
            break;
        }
    }
    closedir(dir);
    wr32(used_ptr, used);
    return WASI_ESUCCESS;
}

static char *path_arg(Wasi *w, uint32_t offset, uint32_t size)
{
    uint8_t *p = at(w, offset, size);
    char *path;
    if (!p) { errno = EFAULT; return NULL; }
    if (size > 4095u) { errno = ENAMETOOLONG; return NULL; }
    if (memchr(p, 0, size)) { errno = EINVAL; return NULL; }
    path = malloc((size_t)size + 1);
    if (!path) { errno = ENOMEM; return NULL; }
    memcpy(path, p, size);
    path[size] = 0;
    return path;
}

static int resolve_dir(Wasi *w, uint32_t dir, const char *path)
{
    int base = descriptor(w, dir);
    if (dir != 3 || base < 0 || !path) return base;
    if (path[0] == '.') return descriptor(w, 4);
    const char *slash = strchr(path, '/');
    size_t comp_len = slash ? (size_t)(slash - path) : strlen(path);
    char comp[256];
    if (comp_len < sizeof(comp)) {
        struct stat st;
        memcpy(comp, path, comp_len);
        comp[comp_len] = '\0';
        if (fstatat(base, comp, &st, AT_SYMLINK_NOFOLLOW) == 0) {
            return base;
        }
    }
    return descriptor(w, 4);
}

uint32_t w2c_wasi__snapshot__preview1_path_open(Wasi *w, uint32_t dir,
    uint32_t lookup, uint32_t offset, uint32_t size, uint32_t oflags,
    uint64_t rights, uint64_t inherit, uint32_t fdflags, uint32_t out)
{
    uint8_t *p = at(w, out, 4);
    char *path;
    unsigned slot;
    int base, flags, fd;
    (void)inherit;
    if (descriptor(w, dir) < 0) return WASI_EBADF;
    if (!p) return WASI_EFAULT;
    path = path_arg(w, offset, size);
    if (!path) return error_code();
    base = resolve_dir(w, dir, path);
    flags = (rights & (1ull << 6)) ? ((rights & 2u) ? O_RDWR : O_WRONLY) : O_RDONLY;
    if (oflags & 1u) flags |= O_CREAT;
    if (oflags & 2u) flags |= O_DIRECTORY;
    if (oflags & 4u) flags |= O_EXCL;
    if (oflags & 8u) flags |= O_TRUNC;
    if (fdflags & 1u) flags |= O_APPEND;
    if (fdflags & 4u) flags |= O_NONBLOCK;
    if (!(lookup & 1u)) flags |= O_NOFOLLOW;
    fd = openat(base, path, flags, 0666);
    if (getenv("EIDOS_WASI_TRACE")) fprintf(stderr, "wasi open fd=%u (base=%d) path=%s flags=%x -> %d errno=%d\n",
        dir, base, path, flags, fd, fd < 0 ? errno : 0);
    free(path);
    if (fd < 0) return error_code();
    for (slot = 5; slot < 256 && w->fds[slot] >= 0; ++slot) { }
    if (slot == 256) { close(fd); return 33; }
    w->fds[slot] = fd;
    wr32(p, slot);
    return 0;
}

static uint32_t path_modify(Wasi *w, uint32_t dir, uint32_t offset, uint32_t size, int op)
{
    int f, rc;
    char *path;
    if (descriptor(w, dir) < 0) return WASI_EBADF;
    path = path_arg(w, offset, size);
    if (!path) return error_code();
    f = resolve_dir(w, dir, path);
    rc = op == 0 ? mkdirat(f, path, 0777) : unlinkat(f, path, op == 2 ? AT_REMOVEDIR : 0);
    if (getenv("EIDOS_WASI_TRACE")) fprintf(stderr, "wasi path op=%d fd=%u (base=%d) path=%s -> %d errno=%d\n",
        op, dir, f, path, rc, rc ? errno : 0);
    free(path);
    return rc ? error_code() : 0;
}

uint32_t w2c_wasi__snapshot__preview1_path_create_directory(Wasi *w, uint32_t fd, uint32_t p, uint32_t n)
{ return path_modify(w, fd, p, n, 0); }
uint32_t w2c_wasi__snapshot__preview1_path_unlink_file(Wasi *w, uint32_t fd, uint32_t p, uint32_t n)
{ return path_modify(w, fd, p, n, 1); }
uint32_t w2c_wasi__snapshot__preview1_path_remove_directory(Wasi *w, uint32_t fd, uint32_t p, uint32_t n)
{ return path_modify(w, fd, p, n, 2); }

uint32_t w2c_wasi__snapshot__preview1_path_rename(Wasi *w, uint32_t oldfd,
    uint32_t oldp, uint32_t oldn, uint32_t newfd, uint32_t newp, uint32_t newn)
{
    int a, b, rc;
    char *oldpath, *newpath;
    if (descriptor(w, oldfd) < 0 || descriptor(w, newfd) < 0) return WASI_EBADF;
    oldpath = path_arg(w, oldp, oldn);
    if (!oldpath) return error_code();
    newpath = path_arg(w, newp, newn);
    if (!newpath) { free(oldpath); return error_code(); }
    a = resolve_dir(w, oldfd, oldpath);
    b = resolve_dir(w, newfd, newpath);
    rc = renameat(a, oldpath, b, newpath);
    free(oldpath);
    free(newpath);
    return rc ? error_code() : 0;
}

uint32_t w2c_wasi__snapshot__preview1_path_filestat_get(Wasi *w, uint32_t dir,
    uint32_t flags, uint32_t offset, uint32_t size, uint32_t out)
{
    uint8_t *p = at(w, out, 64);
    struct stat st;
    char *path;
    int f, rc;
    if (descriptor(w, dir) < 0) return WASI_EBADF;
    if (!p) return WASI_EFAULT;
    path = path_arg(w, offset, size);
    if (!path) return error_code();
    f = resolve_dir(w, dir, path);
    rc = fstatat(f, path, &st, (flags & 1u) ? 0 : AT_SYMLINK_NOFOLLOW);
    free(path);
    if (rc) return error_code();
    memset(p, 0, 64);
    wr64(p, (uint64_t)st.st_dev);
    wr64(p + 8, (uint64_t)st.st_ino);
    p[16] = S_ISDIR(st.st_mode) ? 3 : S_ISREG(st.st_mode) ? 4 : S_ISLNK(st.st_mode) ? 7 : 0;
    wr64(p + 24, (uint64_t)st.st_nlink);
    wr64(p + 32, (uint64_t)st.st_size);
#ifdef __APPLE__
    wr64(p + 40, (uint64_t)st.st_atimespec.tv_sec * 1000000000u + st.st_atimespec.tv_nsec);
    wr64(p + 48, (uint64_t)st.st_mtimespec.tv_sec * 1000000000u + st.st_mtimespec.tv_nsec);
    wr64(p + 56, (uint64_t)st.st_ctimespec.tv_sec * 1000000000u + st.st_ctimespec.tv_nsec);
#else
    wr64(p + 40, (uint64_t)st.st_atim.tv_sec * 1000000000u + st.st_atim.tv_nsec);
    wr64(p + 48, (uint64_t)st.st_mtim.tv_sec * 1000000000u + st.st_mtim.tv_nsec);
    wr64(p + 56, (uint64_t)st.st_ctim.tv_sec * 1000000000u + st.st_ctim.tv_nsec);
#endif
    return 0;
}
