/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper
 * Exercise the actual AROS macros on an AArch64 host, not a guest scheduler.
 */
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <sched.h>
#include <atomic.h>
#ifdef AROS_NO_ATOMIC_OPERATIONS
#error AArch64 selected the UP Disable/Enable fallback
#endif

enum { WORKERS = 4, ITERATIONS = 100003 };
static atomic_int ready, go;
static atomic_int arrived, generation, lost;
static void rendezvous(void)
{
    int g = atomic_load(&generation);
    if (atomic_fetch_add(&arrived, 1) == WORKERS - 1) {
        atomic_store(&arrived, 0);
        atomic_fetch_add(&generation, 1);
    } else {
        while (atomic_load(&generation) == g) sched_yield();
    }
}
static int operation;
#define WIDTH(name, type) \
static struct { type before, value, after; } name = { (type)0xa5, 0, (type)0x5a };
WIDTH(byte, uint8_t)
WIDTH(word, uint16_t)
WIDTH(longword, uint32_t)

#ifdef NEGATIVE_CONTROL
/* Defined individual loads/stores, deliberately NOT an atomic RMW. */
#undef AROS_ATOMIC_OR
#undef AROS_ATOMIC_AND
#define BROKEN(v, mask, op) do { \
    unsigned old = __atomic_load_n(&(v), __ATOMIC_RELAXED); \
    sched_yield(); \
    __atomic_store_n(&(v), old op (mask), __ATOMIC_RELAXED); \
} while (0)
#define AROS_ATOMIC_OR(v, mask) BROKEN(v, mask, |)
#define AROS_ATOMIC_AND(v, mask) BROKEN(v, mask, &)
#endif

#define APPLY(v) do { \
    switch (operation) { \
    case 0: AROS_ATOMIC_INC(v); break; \
    case 1: AROS_ATOMIC_DEC(v); break; \
    case 2: \
        AROS_ATOMIC_OR(v, mask); \
        rendezvous(); \
        if (!(__atomic_load_n(&(v), __ATOMIC_RELAXED) & mask)) atomic_store(&lost, 1); \
        rendezvous(); \
        AROS_ATOMIC_AND(v, ~mask); \
        rendezvous(); \
        if (__atomic_load_n(&(v), __ATOMIC_RELAXED) & mask) atomic_store(&lost, 1); \
        rendezvous(); \
        break; \
    } \
} while (0)

static void *worker(void *arg)
{
    unsigned mask = 1u << (uintptr_t)arg;
    atomic_fetch_add(&ready, 1);
    while (!atomic_load(&go)) sched_yield();
    for (int i = 0; i < (operation == 2 ? 1003 : ITERATIONS); ++i) {
        APPLY(byte.value);
        APPLY(word.value);
        APPLY(longword.value);
    }
    return NULL;
}

int main(void)
{
#ifdef NEGATIVE_CONTROL
    operation = 2;
#else
    operation = 0;
#endif
    for (; operation < 3; ++operation) {
        pthread_t threads[WORKERS];
        unsigned initial = 0;
        byte.value = initial; word.value = initial; longword.value = initial;
        atomic_store(&ready, 0); atomic_store(&go, 0);
        for (uintptr_t i = 0; i < WORKERS; ++i)
            assert(!pthread_create(&threads[i], NULL, worker, (void *)i));
        while (atomic_load(&ready) != WORKERS) sched_yield();
        atomic_store(&go, 1);
        int failed = 0;
        for (int i = 0; i < WORKERS; ++i) {
            void *result;
            assert(!pthread_join(threads[i], &result));
            if (result) failed = 1;
        }
        if (failed || atomic_load(&lost)) { fputs("FAIL: worker lost its own bit\n", stderr); return 1; }
        uint32_t expected = operation == 0 ? WORKERS * ITERATIONS :
            operation == 1 ? 0u - WORKERS * ITERATIONS :
            0u;
        fprintf(stderr, "operation %d: %u/%u/%u expected %u/%u/%u\n", operation,
                byte.value, word.value, longword.value,
                (uint8_t)expected, (uint16_t)expected, expected);
        assert(byte.value == (uint8_t)expected);
        assert(word.value == (uint16_t)expected);
        assert(longword.value == expected);
        assert(byte.before == 0xa5 && byte.after == 0x5a);
        assert(word.before == 0xa5 && word.after == 0x5a);
        assert(longword.before == 0xa5 && longword.after == 0x5a);
    }
    puts("PASS: INC/DEC/AND/OR at 8/16/32 bits; four host threads; wraparound and adjacent guards");
}
