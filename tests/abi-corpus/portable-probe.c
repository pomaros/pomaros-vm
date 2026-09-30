/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 John Knipper */

#include <stdint.h>
#include <dos/dos.h>
#include <dos/stdio.h>
#include <exec/memory.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/utility.h>
#include <workbench/startup.h>

_Static_assert(sizeof(void *) == 8, "AArch64 pointer model");
_Static_assert(sizeof(long) == 8, "AArch64 C long model");
_Static_assert(sizeof(int) == 4, "AArch64 C int model");
_Static_assert(sizeof(IPTR) == 8, "AROS IPTR model");
_Static_assert(sizeof(BPTR) == 8, "AROS BPTR model");
_Static_assert(sizeof(LONG) == 4, "AROS LONG model");

/*
 * This is a CLI command.  Satisfying startup.o's __nowbsupport import and the
 * command-line object's null WBenchMsg reference keeps the Workbench startup
 * object out of the link, so the portable command uses DOS library vectors
 * instead of embedding private struct Process offsets.
 */
int __nowbsupport = 1;
/* The native bring-up has no Intuition/console stack; output is inherited. */
int __nostdiowin = 1;
struct WBStartup *WBenchMsg;

/*
 * Keep this command independent of platform devices and output streams.  Its
 * return value proves that the AROS startup object reached main(), that the
 * loader applied absolute relocations, and that .rodata, .data and .bss have
 * the expected permissions and initial contents.
 */
static const uint64_t probe_constants[] = {
    UINT64_C(0x41524f5341424931),
    UINT64_C(0x7831385245534552),
};
static const uint64_t *probe_relocated_pointer = &probe_constants[1];
static volatile uint64_t probe_data = UINT64_C(0x1020304050607080);
static volatile uint64_t probe_bss;
static volatile uint64_t call_inputs[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
static _Thread_local uint64_t tls_initialized =
    UINT64_C(0x746c732d696e6974);
static _Thread_local uint64_t tls_zero;
static const UBYTE stream_expected[] =
    "AROS Apple Silicon B2.8 boot volume\n";
static const UBYTE probe_output[] = "ABI-PUBLIC-APIS=PASS\n";

typedef uint64_t abi_u64x2 __attribute__((vector_size(16)));

/* Ten arguments force the final pair through the AAPCS64 stack-argument area. */
__attribute__((noinline))
uint64_t abi_integer_call(
    uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4,
    uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8, uint64_t a9)
{
    return a0 + 2U * a1 + 3U * a2 + 4U * a3 + 5U * a4 +
        6U * a5 + 7U * a6 + 8U * a7 + 9U * a8 + 10U * a9;
}

/* Vector arguments and return value exercise the AAPCS64 SIMD register path. */
__attribute__((noinline))
abi_u64x2 abi_vector_call(abi_u64x2 left, abi_u64x2 right)
{
    const abi_u64x2 addend = {UINT64_C(16), UINT64_C(32)};

    return (left ^ right) + addend;
}

static int string_equal(const char *left, const char *right)
{
    if (left == NULL || right == NULL)
        return 0;
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static int bytes_equal(const UBYTE *left, const UBYTE *right, uint32_t size)
{
    uint32_t index;

    for (index = 0; index < size; ++index) {
        if (left[index] != right[index])
            return 0;
    }
    return 1;
}

static int check_calling_convention(void)
{
    const abi_u64x2 left = {UINT64_C(1), UINT64_C(2)};
    const abi_u64x2 right = {UINT64_C(4), UINT64_C(8)};
    abi_u64x2 result;

    if (abi_integer_call(
            call_inputs[0], call_inputs[1], call_inputs[2], call_inputs[3],
            call_inputs[4], call_inputs[5], call_inputs[6], call_inputs[7],
            call_inputs[8], call_inputs[9]) != UINT64_C(385))
        return 0;
    result = abi_vector_call(left, right);
    return result[0] == UINT64_C(21) && result[1] == UINT64_C(42);
}

static int check_exec_and_utility(void)
{
    UBYTE *memory;
    uint32_t index;

    if (FindTask(NULL) == NULL ||
        Stricmp((CONST_STRPTR)"Apple Silicon",
            (CONST_STRPTR)"apple silicon") != 0 ||
        ToUpper((ULONG)'q') != (UBYTE)'Q')
        return 0;
    memory = (UBYTE *)AllocVec(64, MEMF_PUBLIC | MEMF_CLEAR);
    if (memory == NULL)
        return 0;
    for (index = 0; index < 64; ++index) {
        if (memory[index] != 0) {
            FreeVec(memory);
            return 0;
        }
        memory[index] = (UBYTE)(index ^ 0x5aU);
    }
    for (index = 0; index < 64; ++index) {
        if (memory[index] != (UBYTE)(index ^ 0x5aU)) {
            FreeVec(memory);
            return 0;
        }
    }
    FreeVec(memory);
    return 1;
}

static int check_emulated_tls(void)
{
    if (tls_initialized != UINT64_C(0x746c732d696e6974))
        return 35;
    if (tls_zero != 0)
        return 36;
    tls_initialized ^= UINT64_C(0x0102030405060708);
    tls_zero = UINT64_C(0x8877665544332211);
    if (tls_initialized != UINT64_C(0x756e70296c686e7c))
        return 37;
    if (tls_zero != UINT64_C(0x8877665544332211))
        return 38;
    return 0;
}

static int check_dos_stream(const char *path)
{
    UBYTE buffer[sizeof(stream_expected) - 1U];
    BPTR file = Open((CONST_STRPTR)path, MODE_OLDFILE);
    LONG read_result;
    int result = 0;

    if (file == BNULL)
        return 41;
    read_result = Read(file, buffer, 5);
    if (read_result != 5) {
        result = 42;
        goto out;
    }
    if (!bytes_equal(buffer, stream_expected, 5)) {
        result = 43;
        goto out;
    }
    if (Seek(file, 5, OFFSET_BEGINNING) < 0) {
        result = 44;
        goto out;
    }
    if (FGetC(file) != 'A') {
        result = 45;
        goto out;
    }
    if (Seek(file, -1, OFFSET_END) < 0) {
        result = 46;
        goto out;
    }
    if (FGetC(file) != '\n') {
        result = 47;
        goto out;
    }
    if (FGetC(file) != ENDSTREAMCH) {
        result = 48;
        goto out;
    }
    if (Seek(file, 0, OFFSET_BEGINNING) < 0) {
        result = 49;
        goto out;
    }
    read_result = Read(file, buffer, sizeof(buffer));
    if (read_result != (LONG)sizeof(buffer)) {
        result = 50;
        goto out;
    }
    if (!bytes_equal(buffer, stream_expected, sizeof(buffer))) {
        result = 51;
        goto out;
    }
    if (Read(file, buffer, 1) != 0) {
        result = 52;
        goto out;
    }
    if (Flush(file) == 0) {
        result = 53;
        goto out;
    }
out:
    if (!Close(file))
        result = 54;
    return result;
}

int main(int argc, char **argv)
{
    uint64_t value = probe_data;
    int stream_result;

    if (argc != 4)
        return 30;
    if (argv == NULL || argv[0] == NULL ||
        !string_equal(argv[1], "alpha") ||
        !string_equal(argv[2], "two words") || argv[3] == NULL ||
        argv[4] != NULL)
        return 31;

    if (probe_constants[0] != UINT64_C(0x41524f5341424931) ||
        *probe_relocated_pointer != UINT64_C(0x7831385245534552) ||
        probe_bss != 0)
        return 20;

    probe_bss = value ^ UINT64_C(0x1122334455667788);
    if (probe_bss != UINT64_C(0x0102030405060708))
        return 21;
    if (!check_calling_convention())
        return 33;
    if (!check_exec_and_utility())
        return 34;
    stream_result = check_emulated_tls();
    if (stream_result != 0)
        return stream_result;
    stream_result = check_dos_stream(argv[3]);
    if (stream_result != 0)
        return stream_result;
    if (WriteChars(probe_output, sizeof(probe_output) - 1U) !=
            (LONG)(sizeof(probe_output) - 1U))
        return 32;

    return 73;
}
