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
static int operation;
#define WIDTH(name, type) \
static struct { type before, value, after; } name = { (type)0xa5, 0, (type)0x5a };
WIDTH(byte, uint8_t)
WIDTH(word, uint16_t)
WIDTH(longword, uint32_t)

#define APPLY(v) do { \
    switch (operation) { \
    case 0: AROS_ATOMIC_INC(v); break; \
    case 1: AROS_ATOMIC_DEC(v); break; \
    case 2: AROS_ATOMIC_OR(v, mask); break; \
    case 3: AROS_ATOMIC_AND(v, ~mask); break; \
    } \
} while (0)

static void *worker(void *arg)
{
    unsigned mask = 1u << (uintptr_t)arg;
    atomic_fetch_add(&ready, 1);
    while (!atomic_load(&go)) sched_yield();
    for (int i = 0; i < ITERATIONS; ++i) {
        APPLY(byte.value);
        APPLY(word.value);
        APPLY(longword.value);
    }
    return NULL;
}

int main(void)
{
    for (operation = 0; operation < 4; ++operation) {
        pthread_t threads[WORKERS];
        unsigned initial = operation == 3 ? UINT32_MAX : 0;
        byte.value = initial; word.value = initial; longword.value = initial;
        atomic_store(&ready, 0); atomic_store(&go, 0);
        for (uintptr_t i = 0; i < WORKERS; ++i)
            assert(!pthread_create(&threads[i], NULL, worker, (void *)i));
        while (atomic_load(&ready) != WORKERS) sched_yield();
        atomic_store(&go, 1);
        for (int i = 0; i < WORKERS; ++i) assert(!pthread_join(threads[i], NULL));
        uint32_t expected = operation == 0 ? WORKERS * ITERATIONS :
            operation == 1 ? 0u - WORKERS * ITERATIONS :
            operation == 2 ? 15u : ~15u;
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
