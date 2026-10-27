/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
/* Linked guest probe: a code-pin open selects another per-task runtime while
 * the worker must inherit the creator's logical cwd and descriptor namespace. */
#define _LARGEFILE64_SOURCE
#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

static void mark(const char *text)
{
    while (*text) RawPutChar(*text++);
}

static const char token[] = "owned-context";
static int held;
static char creator_directory[512];
static const char *worker_failure;
static ULONG worker_gate, worker_done;

static void *worker(void *unused)
{
    char buffer[sizeof(token)], directory[sizeof(creator_directory)];
    int relative;
    (void)unused;
    while (!__atomic_load_n(&worker_gate, __ATOMIC_ACQUIRE)) Delay(1);
    if (errno != 0)
    {
        worker_failure = "worker initial errno";
        goto done;
    }
    if (lseek(-1, 0, SEEK_SET) != -1 || errno != EBADF)
    {
        worker_failure = "worker shared-module errno";
        goto done;
    }
    if (!getcwd(directory, sizeof(directory)) ||
        strcmp(directory, creator_directory))
    {
        worker_failure = "worker cwd";
        goto done;
    }
    if (read(held, buffer, sizeof(token)) != (ssize_t)sizeof(token) ||
        memcmp(buffer, token, sizeof(token)))
    {
        worker_failure = "captured descriptor";
        goto done;
    }
    relative = open("token", O_RDONLY);
    if (relative < 0)
    {
        worker_failure = "relative open";
        goto done;
    }
    if (read(relative, buffer, sizeof(token)) != (ssize_t)sizeof(token) ||
        memcmp(buffer, token, sizeof(token)))
        worker_failure = "relative contents";
    if (close(relative)) worker_failure = "relative close";
done:
    __atomic_store_n(&worker_done, 1, __ATOMIC_RELEASE);
    return NULL;
}

/* Debugger anchors describe the independently relocated text and bss sections
   of this AROS ELF. The gate is optional; the ordinary smoke stays unattended. */
static volatile ULONG debugger_gate, probe_stage;
static volatile int probe_errno, probe_error;
static volatile LONG probe_ioerr;

static void address(IPTR value)
{
    static const char digits[] = "0123456789abcdef";
    for (int shift = sizeof(value) * 8 - 4; shift >= 0; shift -= 4)
        RawPutChar(digits[(value >> shift) & 15]);
}

static __attribute__((noinline)) int probe_failed(void)
{
    mark("CRT_ADMISSION FAIL: setup, admission, task errno, file resize or cleanup\n");
    return 20;
}

static __attribute__((noinline)) int probe_succeeded(void)
{
    mark("CRT_ADMISSION PASS: selected cwd and descriptors survive pin instance switch; task errno and RAM/FAT resize verified\n");
    return 0;
}

/* Run on both RAM: and the packaged FAT SYS: volume. Native DOS growth
   precedes CRT growth so the CRT fill cannot hide a handler initialization bug. */
static int file_resize(const char *path, ULONG stage)
{
    unsigned char buffer[512];
    BPTR native;
    struct stat sb;
    struct stat64 wide;
    int fd, readonly;
    probe_stage = stage;
    memset(buffer, 0x7b, sizeof(buffer));
    native = Open((CONST_STRPTR)path, MODE_NEWFILE);
    if (!native) return -1;
    if (Write(native, buffer, sizeof(buffer)) != sizeof(buffer)) goto native_failed;
    probe_stage = stage + 1;
    if (Seek(native, 13, OFFSET_BEGINNING) == -1 ||
        SetFileSize(native, 9, OFFSET_BEGINNING) != 9 ||
        Seek(native, 0, OFFSET_CURRENT) != 13 ||
        SetFileSize(native, 3000, OFFSET_BEGINNING) != 3000 ||
        Seek(native, 0, OFFSET_CURRENT) != 13 ||
        Seek(native, 0, OFFSET_BEGINNING) == -1) goto native_failed;
    probe_stage = stage + 2;
    for (int offset = 0; offset < 3000;)
    {
        int count = 3000 - offset;
        if (count > (int)sizeof(buffer)) count = sizeof(buffer);
        if (Read(native, buffer, count) != count) goto native_failed;
        for (int i = 0; i < count; i++)
            if (buffer[i] != (offset + i < 9 ? 0x7b : 0)) goto native_failed;
        offset += count;
    }
    if (!Close(native)) return -1;
    probe_stage = stage + 3;
    fd = open(path, O_RDWR);
    if (fd < 0) return -1;
    if (lseek(fd, 7500, SEEK_SET) != 7500 || ftruncate(fd, 6500) ||
        lseek(fd, 0, SEEK_CUR) != 7500 || fstat(fd, &sb) || sb.st_size != 6500 ||
        fsync(fd)) goto crt_failed;
    probe_stage = stage + 4;
    if (lseek(fd, 0, SEEK_SET) != 0) goto crt_failed;
    for (int offset = 0; offset < 6500;)
    {
        int count = 6500 - offset;
        if (count > (int)sizeof(buffer)) count = sizeof(buffer);
        if (read(fd, buffer, count) != count) goto crt_failed;
        for (int i = 0; i < count; i++)
            if (buffer[i] != (offset + i < 9 ? 0x7b : 0)) goto crt_failed;
        offset += count;
    }
    probe_stage = stage + 5;
    if (lseek64(fd, 7500, SEEK_SET) != 7500 || ftruncate64(fd, 13) ||
        lseek64(fd, 0, SEEK_CUR) != 7500 || fstat64(fd, &wide) || wide.st_size != 13 ||
        ftruncate64(fd, 2000) || lseek64(fd, 0, SEEK_CUR) != 7500)
        goto crt_failed;
    errno = 0;
    if (ftruncate64(fd, -1) != -1 || errno != EINVAL) goto crt_failed;
    if (close(fd)) return -1;
    probe_stage = stage + 6;
    readonly = open(path, O_RDONLY);
    if (readonly < 0) return -1;
    errno = 0;
    int refused = ftruncate(readonly, 1), error = errno;
    if (close(readonly) || refused != -1 || error != EBADF || unlink(path)) return -1;
    return 0;
crt_failed:
    {
        int error = errno; LONG ioerror = IoErr();
        close(fd); errno = error; SetIoErr(ioerror);
        return -1;
    }
native_failed:
    {
        int error = errno; LONG ioerror = IoErr();
        Close(native); errno = error; SetIoErr(ioerror);
        return -1;
    }
}

/* Native DOS CurrentDir remains authoritative when a CRT is loaded. NetDB
   temporarily selects its db directory before opening a relative include. */
static int native_current_directory(void)
{
    BPTR directory, old, file;
    LONG error;
    probe_stage = 100;
    directory = CreateDir((CONST_STRPTR)"RAM:RuntimeNativeDirectory");
    if (!directory) return -1;
    /* CreateDir returns an exclusive lock, which DOS cannot duplicate.
       Match NetDB's shared Lock before selecting the native directory. */
    UnLock(directory);
    directory = Lock((CONST_STRPTR)"RAM:RuntimeNativeDirectory", SHARED_LOCK);
    if (!directory) return -1;
    file = Open((CONST_STRPTR)"RAM:RuntimeNativeDirectory/marker", MODE_NEWFILE);
    if (!file) { UnLock(directory); return -1; }
    Close(file);
    old = CurrentDir(directory);
    probe_stage = 101;
    file = Open((CONST_STRPTR)"marker", MODE_OLDFILE);
    error = IoErr();
    UnLock(CurrentDir(old));
    if (file) Close(file);
    DeleteFile((CONST_STRPTR)"RAM:RuntimeNativeDirectory/marker");
    DeleteFile((CONST_STRPTR)"RAM:RuntimeNativeDirectory");
    if (!file) {
        mark("CRT_NATIVE_CWD FAIL: native CurrentDir ignored by relative DOS Open\n");
        SetIoErr(error);
        return -1;
    }
    mark("CRT_NATIVE_CWD PASS: native CurrentDir selects relative DOS Open\n");
    return 0;
}

int main(int argc, char **argv)
{
    BPTR directory, original_input, shadow;
    pthread_t thread;
    int error, creator_errno;
    char *original_directory;
    const char *resize_path = NULL;
    int debug = 0;
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--debugger")) debug = 1;
        else if (!strcmp(argv[i], "--resize-path") && i + 1 < argc)
            resize_path = argv[++i];
        else return 20;
    }
    if (!resize_path)
    {
        mark("CRT_ADMISSION FAIL: --resize-path on a writable FAT volume required\n");
        return 20;
    }
    if (debug)
    {
        mark("CRT_DEBUG main="); address((IPTR)main);
        mark(" gate="); address((IPTR)&debugger_gate); mark("\n");
        while (!debugger_gate) Delay(1);
    }
    probe_stage = 1;
    original_directory = getcwd(NULL, 0);
    if (!original_directory) goto failed;
    if (native_current_directory()) goto failed;
    probe_stage = 2;
    directory = CreateDir((CONST_STRPTR)"RAM:RuntimeContextProbe");
    if (!directory) goto failed;
    UnLock(directory);
    probe_stage = 3;
    if (chdir("RAM:RuntimeContextProbe")) goto failed;
    probe_stage = 4;
    if (!getcwd(creator_directory, sizeof(creator_directory))) goto failed;
    probe_stage = 5;
    held = open("token", O_CREAT | O_TRUNC | O_RDWR, 0600);
    if (held < 0) goto failed;
    probe_stage = 6;
    if (write(held, token, sizeof(token)) != (ssize_t)sizeof(token)) goto failed;
    probe_stage = 7;
    if (lseek(held, 0, SEEK_SET) != 0) goto failed;
    probe_stage = 8;
    shadow = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);
    if (!shadow) goto failed;
    original_input = SelectInput(shadow);
    /* The generator compares native stdin as well as the return address:
       this pin cannot simply reuse the creator's existing runtime base. */
    probe_stage = 9;
    probe_error = error = pthread_create(&thread, NULL, worker, NULL);
    SelectInput(original_input);
    Close(shadow);
    if (error) goto failed;
    probe_stage = 16;
    errno = EDOM;
    __atomic_store_n(&worker_gate, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&worker_done, __ATOMIC_ACQUIRE)) Delay(1);
    creator_errno = errno;
    probe_stage = 10;
    probe_error = error = pthread_join(thread, NULL);
    if (error) goto failed;
    if (creator_errno != EDOM || errno != EDOM)
    {
        worker_failure = "creator errno isolation";
    }
    probe_stage = 11;
    if (worker_failure)
    {
        mark("CRT_ADMISSION FAIL: "); mark(worker_failure); mark("\n");
        return 20;
    }
    probe_stage = 12;
    if (close(held)) goto failed;
    probe_stage = 13;
    if (unlink("token")) goto failed;
    probe_stage = 14;
    if (chdir(original_directory)) goto failed;
    probe_stage = 15;
    if (rmdir("RAM:RuntimeContextProbe")) goto failed;
    free(original_directory);
    if (file_resize("RAM:RuntimeResizeProbe", 30) ||
        file_resize(resize_path, 60)) goto failed;
    return probe_succeeded();
failed:
    probe_errno = errno;
    probe_ioerr = IoErr();
    return probe_failed();
}
