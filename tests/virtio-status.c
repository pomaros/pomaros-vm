// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <assert.h>
#include <stdio.h>
#include "status.h"
struct Device {
    unsigned reads, writes, reset_after;
    uint8_t status, history[4];
    int reject_driver;
};
static uint8_t read_status(void *context)
{
    struct Device *d = context;
    ++d->reads;
    if (d->writes == 1 && d->reads == d->reset_after) d->status = 0;
    return d->status;
}
static void write_status(void *context, uint8_t value)
{
    struct Device *d = context;
    assert(d->writes < 4);
    d->history[d->writes++] = value;
    if (value) {
        assert(d->reads >= d->reset_after && d->reset_after);
        d->status = d->reject_driver ? 0x80 : value;
    }
}
int main(void)
{
    struct Device d;
    struct VirtioStatusAccess io = {&d, read_status, write_status};
    /* Delayed reset at every boundary, including the final permitted read. */
    for (unsigned n = 1; n <= 4; ++n) {
        d = (struct Device){.status=15, .reset_after=n};
        assert(VirtioStatusReset(&io, 4));
        assert(d.reads == n && d.writes == 1 && !d.status);
        d = (struct Device){.status=15, .reset_after=n};
        assert(VirtioStatusBegin(&io, 4));
        assert(d.reads == n+1 && d.writes == 3 && d.status == 3);
        assert(d.history[0] == 0 && d.history[1] == 1 && d.history[2] == 3);
    }
    /* Permanent refusal and acknowledgement one read too late must not
     * publish ACKNOWLEDGE or DRIVER. This catches the old spin-- wrap bug. */
    for (unsigned n = 0; n <= 5; n += 5) {
        d = (struct Device){.status=15, .reset_after=n};
        assert(!VirtioStatusBegin(&io, 4));
        assert(d.reads == 4 && d.writes == 1 && d.status == 15);
    }
    d = (struct Device){.status=15, .reset_after=1, .reject_driver=1};
    assert(!VirtioStatusBegin(&io, 4));
    d = (struct Device){.status=15};
    assert(!VirtioStatusReset(&io, 0));assert(!d.reads && !d.writes);
    assert(!VirtioStatusReset(NULL, 1));
    io.read = NULL;assert(!VirtioStatusReset(&io, 1));
    puts("virtio-status result=PASS delayed,deadline,refusal,no-restart-on-timeout");
}
