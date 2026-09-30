/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 John Knipper */

#include <stdint.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <exec/types.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <workbench/startup.h>

extern struct WBStartup *WBenchMsg;

_Static_assert(sizeof(void *) == 8, "AArch64 pointer model");
_Static_assert(sizeof(long) == 8, "AArch64 C long model");
_Static_assert(sizeof(int) == 4, "AArch64 C int model");
_Static_assert(sizeof(IPTR) == 8, "AROS IPTR model");
_Static_assert(sizeof(BPTR) == 8, "AROS BPTR model");
_Static_assert(sizeof(LONG) == 4, "AROS LONG model");

/* Keep Workbench startup but suppress command-line and stdio-window helpers. */
int __nocommandline = 1;
int __nostdiowin = 1;

static const uint64_t probe_constants[] = {
    UINT64_C(0x41524f5357424142),
    UINT64_C(0x7831385742454e43),
};
static const uint64_t *probe_relocated_pointer = &probe_constants[1];
static volatile uint64_t probe_data = UINT64_C(0x1020304050607080);
static volatile uint64_t probe_bss;

static int string_equal(const BYTE *left, const BYTE *right)
{
    if (left == NULL || right == NULL)
        return 0;
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

static int write_verdict(const char *message, LONG length)
{
    BPTR output = Open((CONST_STRPTR)"MacRW:WB-ABI.result", MODE_NEWFILE);
    LONG written;

    if (output == BNULL)
        return 90;
    written = Write(output, message, length);
    Close(output);
    return written == length ? 0 : 91;
}

int main(int argc, char **argv)
{
    static const char pass[] =
        "WB-ABI result=PASS argc=0 argv=wbenchmsg numargs=1 "
        "process=match lock=present name=WBPROBE\n";
    static const char fail_argc[] = "WB-ABI result=FAIL reason=argc\n";
    static const char fail_message[] = "WB-ABI result=FAIL reason=message\n";
    static const char fail_args[] = "WB-ABI result=FAIL reason=arguments\n";
    static const char fail_process[] = "WB-ABI result=FAIL reason=process\n";
    static const char fail_memory[] = "WB-ABI result=FAIL reason=memory\n";
    struct Process *process = (struct Process *)FindTask(NULL);

    if (argc != 0) {
        write_verdict(fail_argc, sizeof(fail_argc) - 1U);
        return 30;
    }
    if (WBenchMsg == NULL || argv != (char **)WBenchMsg) {
        write_verdict(fail_message, sizeof(fail_message) - 1U);
        return 31;
    }
    if (WBenchMsg->sm_NumArgs != 1 || WBenchMsg->sm_ArgList == NULL ||
            WBenchMsg->sm_ArgList[0].wa_Lock == BNULL ||
            !string_equal(WBenchMsg->sm_ArgList[0].wa_Name,
                          (const BYTE *)"WBPROBE")) {
        write_verdict(fail_args, sizeof(fail_args) - 1U);
        return 32;
    }
    if (process == NULL || WBenchMsg->sm_Process != &process->pr_MsgPort) {
        write_verdict(fail_process, sizeof(fail_process) - 1U);
        return 33;
    }
    if (probe_constants[0] != UINT64_C(0x41524f5357424142) ||
            *probe_relocated_pointer != UINT64_C(0x7831385742454e43) ||
            probe_bss != 0) {
        write_verdict(fail_memory, sizeof(fail_memory) - 1U);
        return 20;
    }
    probe_bss = probe_data ^ UINT64_C(0x1122334455667788);
    if (probe_bss != UINT64_C(0x0102030405060708)) {
        write_verdict(fail_memory, sizeof(fail_memory) - 1U);
        return 21;
    }

    {
        int verdict = write_verdict(pass, sizeof(pass) - 1U);

        /* Native B2 has no MacRW: handler; its parent observes return 73. */
        return verdict == 90 ? 73 : verdict;
    }
}
