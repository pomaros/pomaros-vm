/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 John Knipper */

#include <stdint.h>
#include <pthread.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <workbench/startup.h>

_Static_assert(sizeof(void *) == 8, "AArch64 pointer model");
_Static_assert(sizeof(pthread_t) == 4, "AROS pthread_t model");

/* Keep the CLI on public startup interfaces and out of Workbench startup. */
int __nowbsupport = 1;
int __nostdiowin = 1;
struct WBStartup *WBenchMsg;

static _Thread_local uint64_t tls_initialized =
    UINT64_C(0x746c732d696e6974);
static _Thread_local uint64_t tls_zero;
static pthread_mutex_t test_mutex;
static pthread_cond_t ready_cond;
static pthread_cond_t release_cond;
static uint32_t ready_count;
static int release_workers;
static const UBYTE pass_output[] = "ABI-TLS-THREADS=PASS\n";

struct tls_worker {
    uint64_t initialized_value;
    uint64_t zero_value;
    const void *initialized_address;
    const void *zero_address;
    int error;
};

static void *worker_entry(void *argument)
{
    struct tls_worker *worker = (struct tls_worker *)argument;

    if (tls_initialized != UINT64_C(0x746c732d696e6974))
        worker->error = 91;
    else if (tls_zero != 0)
        worker->error = 92;
    tls_initialized = worker->initialized_value;
    tls_zero = worker->zero_value;
    worker->initialized_address = (const void *)&tls_initialized;
    worker->zero_address = (const void *)&tls_zero;
    if (tls_initialized != worker->initialized_value ||
            tls_zero != worker->zero_value)
        worker->error = 93;

    if (pthread_mutex_lock(&test_mutex) != 0) {
        worker->error = 94;
        return NULL;
    }
    ++ready_count;
    (void)pthread_cond_broadcast(&ready_cond);
    while (!release_workers) {
        if (pthread_cond_wait(&release_cond, &test_mutex) != 0) {
            worker->error = 95;
            break;
        }
    }
    (void)pthread_mutex_unlock(&test_mutex);

    if (tls_initialized != worker->initialized_value ||
            tls_zero != worker->zero_value)
        worker->error = 96;
    return NULL;
}

static int check_threads(void)
{
    pthread_t threads[2];
    struct tls_worker workers[2] = {
        {UINT64_C(0x1112131415161718), UINT64_C(0x2122232425262728),
            NULL, NULL, 0},
        {UINT64_C(0x3132333435363738), UINT64_C(0x4142434445464748),
            NULL, NULL, 0},
    };
    uint32_t created = 0;
    uint32_t index;
    int result = 0;

    ready_count = 0;
    release_workers = 0;
    if (pthread_mutex_init(&test_mutex, NULL) != 0)
        return 80;
    if (pthread_cond_init(&ready_cond, NULL) != 0) {
        (void)pthread_mutex_destroy(&test_mutex);
        return 81;
    }
    if (pthread_cond_init(&release_cond, NULL) != 0) {
        (void)pthread_cond_destroy(&ready_cond);
        (void)pthread_mutex_destroy(&test_mutex);
        return 82;
    }
    for (index = 0; index < 2; ++index) {
        if (pthread_create(&threads[index], NULL, worker_entry,
                &workers[index]) != 0) {
            result = 83 + (int)index;
            break;
        }
        ++created;
    }

    if (pthread_mutex_lock(&test_mutex) != 0)
        result = 84;
    else {
        while (result == 0 && ready_count != created) {
            if (pthread_cond_wait(&ready_cond, &test_mutex) != 0)
                result = 86;
        }
        if (result == 0 && created == 2) {
            if (workers[0].initialized_address == NULL ||
                    workers[0].zero_address == NULL ||
                    workers[1].initialized_address == NULL ||
                    workers[1].zero_address == NULL)
                result = 87;
            else if (workers[0].initialized_address ==
                        workers[1].initialized_address ||
                    workers[0].zero_address == workers[1].zero_address ||
                    workers[0].initialized_address ==
                        (const void *)&tls_initialized ||
                    workers[1].initialized_address ==
                        (const void *)&tls_initialized ||
                    workers[0].zero_address == (const void *)&tls_zero ||
                    workers[1].zero_address == (const void *)&tls_zero)
                result = 88;
            else if (tls_initialized != UINT64_C(0x5152535455565758) ||
                    tls_zero != UINT64_C(0x6162636465666768))
                result = 89;
        }
        release_workers = 1;
        (void)pthread_cond_broadcast(&release_cond);
        (void)pthread_mutex_unlock(&test_mutex);
    }

    for (index = 0; index < created; ++index) {
        if (pthread_join(threads[index], NULL) != 0 && result == 0)
            result = 90;
        if (workers[index].error != 0 && result == 0)
            result = workers[index].error;
    }
    if (pthread_cond_destroy(&release_cond) != 0 && result == 0)
        result = 97;
    if (pthread_cond_destroy(&ready_cond) != 0 && result == 0)
        result = 98;
    if (pthread_mutex_destroy(&test_mutex) != 0 && result == 0)
        result = 99;
    return result;
}

int main(int argc, char **argv)
{
    int result;

    if (argc != 1 || argv == NULL || argv[0] == NULL || argv[1] != NULL)
        return 30;
    if (tls_initialized != UINT64_C(0x746c732d696e6974) || tls_zero != 0)
        return 31;
    tls_initialized = UINT64_C(0x5152535455565758);
    tls_zero = UINT64_C(0x6162636465666768);
    result = check_threads();
    if (result != 0)
        return result;
    if (tls_initialized != UINT64_C(0x5152535455565758) ||
            tls_zero != UINT64_C(0x6162636465666768))
        return 100;
    if (WriteChars(pass_output, sizeof(pass_output) - 1U) !=
            (LONG)(sizeof(pass_output) - 1U))
        return 101;
    return 74;
}
