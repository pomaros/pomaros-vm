/* Copyright (C) 2026 John KNIPPER <code@jkn.me>
 * Distributed under the AROS Public License 1.1. */
/* Linked guest probe: a code-pin open selects another per-task runtime while
 * the worker must inherit the creator's logical cwd and descriptor namespace. */
#include <dos/dos.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static void mark(const char *text)
{
    while (*text) RawPutChar(*text++);
}

static const char token[] = "owned-context";
static int held;
static char creator_directory[512];
static const char *worker_failure;

static void *worker(void *unused)
{
    char buffer[sizeof(token)], directory[sizeof(creator_directory)];
    int relative;
    (void)unused;
    if (!getcwd(directory, sizeof(directory)) ||
        strcmp(directory, creator_directory))
    {
        worker_failure = "worker cwd";
        return NULL;
    }
    if (read(held, buffer, sizeof(token)) != (ssize_t)sizeof(token) ||
        memcmp(buffer, token, sizeof(token)))
    {
        worker_failure = "captured descriptor";
        return NULL;
    }
    relative = open("token", O_RDONLY);
    if (relative < 0)
    {
        worker_failure = "relative open";
        return NULL;
    }
    if (read(relative, buffer, sizeof(token)) != (ssize_t)sizeof(token) ||
        memcmp(buffer, token, sizeof(token)))
        worker_failure = "relative contents";
    if (close(relative)) worker_failure = "relative close";
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
    mark("CRT_ADMISSION FAIL: creator setup, admission, join or cleanup\n");
    return 20;
}

static __attribute__((noinline)) int probe_succeeded(void)
{
    mark("CRT_ADMISSION PASS: selected cwd and descriptors survive pin instance switch\n");
    return 0;
}

int main(int argc, char **argv)
{
    BPTR directory, original_input, shadow;
    pthread_t thread;
    int error;
    char *original_directory;
    if (argc == 2 && !strcmp(argv[1], "--debugger"))
    {
        mark("CRT_DEBUG main="); address((IPTR)main);
        mark(" gate="); address((IPTR)&debugger_gate); mark("\n");
        while (!debugger_gate) Delay(1);
    }
    else if (argc != 1) return 20;
    probe_stage = 1;
    original_directory = getcwd(NULL, 0);
    if (!original_directory) goto failed;
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
    probe_stage = 10;
    probe_error = error = pthread_join(thread, NULL);
    if (error) goto failed;
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
    return probe_succeeded();
failed:
    probe_errno = errno;
    probe_ioerr = IoErr();
    return probe_failed();
}
