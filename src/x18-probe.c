// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
#include <Hypervisor/Hypervisor.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#define CHECK(x) do { hv_return_t r = (x); if (r != HV_SUCCESS) { \
    fprintf(stderr, "%s: 0x%x\n", #x, r); exit(1); } } while (0)
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    size_t size = (size_t)getpagesize();
    void *ram = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    if (ram == MAP_FAILED) return 1;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    size_t n = fread(ram, 1, size, f);
    if (ferror(f) || !n || n == size) return 1;
    fclose(f);
    CHECK(hv_vm_create(NULL));
    CHECK(hv_vm_map(ram, 0x40000000, size, HV_MEMORY_READ|HV_MEMORY_EXEC));
    hv_vcpu_t cpu; hv_vcpu_exit_t *event;
    CHECK(hv_vcpu_create(&cpu, &event, NULL));
    CHECK(hv_vcpu_set_reg(cpu, HV_REG_PC, 0x40000000));
    CHECK(hv_vcpu_set_reg(cpu, HV_REG_CPSR, 0x3c5));
    for (unsigned i=0; i<2; i++) {
        CHECK(hv_vcpu_run(cpu));
        uint64_t value, pc;
        CHECK(hv_vcpu_get_reg(cpu, HV_REG_X18, &value));
        CHECK(hv_vcpu_get_reg(cpu, HV_REG_PC, &pc));
        if (event->reason != HV_EXIT_REASON_EXCEPTION ||
            (event->exception.syndrome >> 26) != 0x16 ||
            (event->exception.syndrome & 0xffff) != 0x18+i ||
            value != 0x1234+i) {
            fprintf(stderr,"FAIL reason=%u esr=%llx pc=%llx x18=%llx\n",
                event->reason, event->exception.syndrome, pc, value);
            return 1;
        }
        printf("guest EL1 exit=%u x18=0x%llx pc=0x%llx\n",i,value,pc);
        /* HVC's preferred return address already points at the next instruction. */
        usleep(10000);
    }
    CHECK(hv_vcpu_destroy(cpu));
    CHECK(hv_vm_unmap(0x40000000,size));
    CHECK(hv_vm_destroy());
    munmap(ram,size);
    puts("x18-probe result=PASS backend=Hypervisor.framework guest=EL1 exits=2");
}
