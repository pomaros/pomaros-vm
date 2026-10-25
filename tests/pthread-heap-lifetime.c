/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 John Knipper
 */
#include <aros/debug.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_BYTES 8192
#define CYCLES 32

static void *produce(void *argument)
{
    unsigned char *block = malloc(BLOCK_BYTES);
    if (block) memset(block, (int)(uintptr_t)argument, BLOCK_BYTES);
    return block;
}

static void *consume(void *argument)
{
    unsigned char *block = argument;
    for (size_t n = 0; n < BLOCK_BYTES; ++n)
        if (block[n] != 0xa5) return (void *)(uintptr_t)1;
    free(block);
    return NULL;
}

int main(void)
{
    for (unsigned cycle = 0; cycle < CYCLES; ++cycle) {
        pthread_t worker;
        void *result = NULL;
        unsigned char pattern = (unsigned char)(cycle + 1);
        if (pthread_create(&worker, NULL, produce, (void *)(uintptr_t)pattern) ||
            pthread_join(worker, &result) || !result) goto fail;
        /* A returned allocation belongs to the process, not the ended worker.
           Churn another opener before inspecting the transferred object. */
        void *churn = malloc(BLOCK_BYTES * 2);
        if (!churn) goto fail;
        memset(churn, 0x5a, BLOCK_BYTES * 2);
        unsigned char *block = result;
        for (size_t n = 0; n < BLOCK_BYTES; ++n)
            if (block[n] != pattern) goto fail;
        free(churn);
        free(block);
        block = malloc(BLOCK_BYTES);
        if (!block) goto fail;
        memset(block, 0xa5, BLOCK_BYTES);
        if (pthread_create(&worker, NULL, consume, block) ||
            pthread_join(worker, &result) || result) goto fail;
    }
    bug("[VM] pthread heap survives worker exit PASS cycles=%u\n", CYCLES);
    return 0;
fail:
    bug("FAIL: pthread heap lifetime\n");
    return 20;
}
