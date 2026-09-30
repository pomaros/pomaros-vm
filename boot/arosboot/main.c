// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper

#include "uefi.h"
#include "allocation_tracker.h"
#include "bundle_loader.h"
#include <aros/apple/bootinfo.h>

#define FDT_MAGIC UINT32_C(0xd00dfeed)
#define FDT_MAX_SIZE (16U * 1024U * 1024U)
#define MEMORY_MAP_ATTEMPTS 8U
#define HANDOFF_STACK_SIZE (64U * 1024U)
#define TEST_KERNEL_MEMORY_SIZE (16U * 1024U * 1024U)

#ifndef AROSBOOT_FRAME_DIAGNOSTIC
#define AROSBOOT_FRAME_DIAGNOSTIC 0
#endif

#ifndef AROSBOOT_FRAME_DIAGNOSTIC_LATCH
#define AROSBOOT_FRAME_DIAGNOSTIC_LATCH 0
#endif

#ifndef AROSBOOT_FRAME_DIAGNOSTIC_DELAY_US
#define AROSBOOT_FRAME_DIAGNOSTIC_DELAY_US 2000000U
#endif

#define DIAGNOSTIC_PHASE_DELAY_US \
    ((EFI_UINTN)AROSBOOT_FRAME_DIAGNOSTIC_DELAY_US)

void arosboot_enter_payload(
    struct AROSBootInfo *, uintptr_t, uintptr_t) __attribute__((noreturn));

static const struct EFI_GUID dtb_table_guid = {
    0xb1b621d5, 0xf19c, 0x41a5,
    {0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0}
};

static const struct EFI_GUID graphics_output_guid = {
    0x9042a9de, 0x23dc, 0x4a38,
    {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}
};

static const char loader_log[] =
    "AROSBOOTAA64 EFI loader; boot ABI v3; producer=UEFI; explicit ownership";

static int guid_equal(const struct EFI_GUID *left, const struct EFI_GUID *right)
{
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    size_t index;

    for (index = 0; index < sizeof(*left); ++index) {
        if (a[index] != b[index])
            return 0;
    }
    return 1;
}

static uint32_t read_be32(const void *address)
{
    const uint8_t *bytes = (const uint8_t *)address;
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static struct EFI_FILE_PROTOCOL *boot_log;
static void open_boot_log(EFI_HANDLE image, struct EFI_SYSTEM_TABLE *system)
{
    static const struct EFI_GUID loaded_guid = {0x5b1b31a1,0x9562,0x11d2,{0x8e,0x3f,0,0xa0,0xc9,0x69,0x72,0x3b}};
    static const struct EFI_GUID fs_guid = {0x964e5b22,0x6459,0x11d2,{0x8e,0x39,0,0xa0,0xc9,0x69,0x72,0x3b}};
    struct EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    struct EFI_FILE_PROTOCOL *root = NULL;
    EFI_CHAR16 path[] = {'B','O','O','T','.','L','O','G',0};
    if (EFI_ERROR(system->boot_services->handle_protocol(image,&loaded_guid,(void **)&loaded)) ||
        EFI_ERROR(system->boot_services->handle_protocol(loaded->device_handle,&fs_guid,(void **)&fs)) ||
        EFI_ERROR(fs->open_volume(fs,&root))) return;
    struct EFI_FILE_PROTOCOL *old = NULL;
    if (!EFI_ERROR(root->open(root,&old,path,3,0)) && old) (void)old->delete_file(old);
    (void)root->open(root,&boot_log,path,UINT64_C(0x8000000000000003),0);
    root->close(root);
}

static void print_ascii(struct EFI_SYSTEM_TABLE *system, const char *text)
{
    EFI_CHAR16 buffer[128];
    if (boot_log) {
        EFI_UINTN bytes = 0;
        while (text[bytes]) ++bytes;
        (void)boot_log->write(boot_log,&bytes,(void *)text);
        (void)boot_log->flush(boot_log);
    }

    while (*text != '\0') {
        size_t length = 0;
        while (text[length] != '\0' && length + 1 < sizeof(buffer) / sizeof(buffer[0])) {
            buffer[length] = (EFI_CHAR16)(uint8_t)text[length];
            ++length;
        }
        buffer[length] = 0;
        system->console_out->output_string(system->console_out, buffer);
        text += length;
    }
}

static void print_hex64(struct EFI_SYSTEM_TABLE *system, uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    char text[19];
    unsigned int shift;

    text[0] = '0';
    text[1] = 'x';
    for (shift = 0; shift < 16; ++shift)
        text[2 + shift] = digits[(value >> ((15U - shift) * 4U)) & 0xfU];
    text[18] = '\0';
    print_ascii(system, text);
}

static void clean_range_to_poc(uint64_t physical, uint64_t size)
{
    uintptr_t first;
    uintptr_t last;
    uintptr_t line;
    uint64_t ctr;
    uintptr_t line_size;

    if (physical == 0 || size == 0 || physical > UINTPTR_MAX ||
        size - 1U > UINTPTR_MAX - (uintptr_t)physical)
        return;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    line_size = (uintptr_t)4U << ((ctr >> 16) & 0xfU);
    first = (uintptr_t)physical & ~(line_size - 1U);
    last = (uintptr_t)physical + (uintptr_t)size - 1U;
    for (line = first; line <= last; line += line_size) {
        __asm__ volatile("dc cvac, %0" : : "r"(line) : "memory");
        if (UINTPTR_MAX - line < line_size)
            break;
    }
}

#if AROSBOOT_FRAME_DIAGNOSTIC
/*
 * The installed Apple path loses its serial console when m1n1 vectors into
 * U-Boot.  This deliberately simple framebuffer code makes the EFI boundary
 * observable without importing a font renderer or depending on firmware
 * console state.  A row of white blocks identifies the completed phase.  A
 * coloured failure screen additionally encodes the low EFI status byte in the
 * bottom row and then stops, preserving the first failure for inspection.
 */
static void draw_diagnostic_pattern(
    uint64_t physical, uint64_t size, uint32_t width, uint32_t height,
    uint32_t stride, uint32_t phase, uint64_t status, int failed)
{
    volatile uint32_t *pixels = (volatile uint32_t *)(uintptr_t)physical;
    uint64_t available_pixels = size / sizeof(uint32_t);
    uint32_t block_width;
    uint32_t block_height;
    uint32_t background;
    uint32_t x;
    uint32_t y;

    if (physical == 0 || pixels == NULL || width < 32U || height < 24U ||
        stride < width || (uint64_t)stride * height > available_pixels)
        return;

    background = failed ? UINT32_C(0x000000a0) :
        UINT32_C(0x00181818) * ((phase & 7U) + 1U);
    block_width = width / 16U;
    block_height = height / 12U;
    if (block_width == 0 || block_height == 0)
        return;

    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            uint32_t color = background;
            uint32_t block = x / block_width;

            if (y < block_height && block < 8U)
                color = block < phase ? UINT32_C(0x00f0f0f0) : 0;
            if (failed && y + block_height >= height && block < 8U)
                color = ((status >> block) & 1U) != 0 ?
                    UINT32_C(0x00f0f0f0) : 0;
            pixels[(uint64_t)y * stride + x] = color;
        }
    }
    clean_range_to_poc(physical, size);
    __asm__ volatile("dsb sy" : : : "memory");
}

static void draw_efi_diagnostic(
    struct EFI_SYSTEM_TABLE *system, uint32_t phase, uint64_t status,
    int failed)
{
    struct EFI_GRAPHICS_OUTPUT_PROTOCOL *graphics = NULL;
    EFI_STATUS locate_status = system->boot_services->locate_protocol(
        &graphics_output_guid, NULL, (void **)&graphics);

    if (!EFI_ERROR(locate_status) && graphics != NULL &&
        graphics->mode != NULL && graphics->mode->info != NULL) {
        draw_diagnostic_pattern(
            graphics->mode->frame_buffer_base,
            graphics->mode->frame_buffer_size,
            graphics->mode->info->horizontal_resolution,
            graphics->mode->info->vertical_resolution,
            graphics->mode->info->pixels_per_scan_line,
            phase, status, failed);
    }
#if AROSBOOT_FRAME_DIAGNOSTIC_LATCH
    if (system->boot_services->stall != NULL)
        (void)system->boot_services->stall(DIAGNOSTIC_PHASE_DELAY_US);
#endif
}

static EFI_STATUS stop_with_diagnostic(
    struct EFI_SYSTEM_TABLE *system, uint32_t phase, EFI_STATUS status)
{
    draw_efi_diagnostic(system, phase, status, 1);
    for (;;)
        __asm__ volatile("wfe");
}
#else
static void draw_efi_diagnostic(
    struct EFI_SYSTEM_TABLE *system, uint32_t phase, uint64_t status,
    int failed)
{
    (void)system;
    (void)phase;
    (void)status;
    (void)failed;
}

static EFI_STATUS stop_with_diagnostic(
    struct EFI_SYSTEM_TABLE *system, uint32_t phase, EFI_STATUS status)
{
    (void)system;
    (void)phase;
    if (boot_log) { boot_log->close(boot_log); boot_log = NULL; }
    return status;
}
#endif

static void clean_handoff_to_poc(
    struct AROSBootInfo *boot_info, void *stack)
{
    uint32_t index;

    clean_range_to_poc(boot_info->fdt_physical, boot_info->fdt_size);
    clean_range_to_poc(
        boot_info->memory_map_physical, boot_info->memory_map_size);
    clean_range_to_poc(
        boot_info->loader_log_physical, boot_info->loader_log_size);
    clean_range_to_poc(
        boot_info->allocations_physical,
        (uint64_t)boot_info->allocation_count *
            boot_info->allocation_entry_size);
    clean_range_to_poc((uint64_t)(uintptr_t)stack, HANDOFF_STACK_SIZE);

    if ((boot_info->flags & AROS_BOOTINFO_FLAG_MODULES) != 0) {
        const struct AROSBootModule *modules =
            (const struct AROSBootModule *)(uintptr_t)boot_info->modules_physical;
        clean_range_to_poc(
            boot_info->modules_physical,
            (uint64_t)boot_info->module_count * boot_info->module_entry_size);
        for (index = 0; index < boot_info->module_count; ++index) {
            const struct AROSBootModule *module =
                (const struct AROSBootModule *)((const uint8_t *)modules +
                    (uint64_t)index * boot_info->module_entry_size);
            clean_range_to_poc(module->physical_address, module->size);
            clean_range_to_poc(module->name_physical, module->name_size);
        }
    }

    clean_range_to_poc(
        (uint64_t)(uintptr_t)boot_info, sizeof(*boot_info));
    __asm__ volatile("dsb sy" : : : "memory");
}

static EFI_STATUS copy_fdt(
    struct EFI_SYSTEM_TABLE *system, struct AROSBootInfo *boot_info)
{
    struct EFI_BOOT_SERVICES *boot = system->boot_services;
    EFI_UINTN index;

    for (index = 0; index < system->number_of_table_entries; ++index) {
        struct EFI_CONFIGURATION_TABLE *entry = &system->configuration_table[index];
        const uint8_t *source;
        uint32_t size;
        void *copy;
        EFI_STATUS status;

        if (!guid_equal(&entry->vendor_guid, &dtb_table_guid))
            continue;
        source = (const uint8_t *)entry->vendor_table;
        if (source == NULL || read_be32(source) != FDT_MAGIC)
            return EFI_INVALID_PARAMETER;
        size = read_be32(source + 4);
        if (size < 40U || size > FDT_MAX_SIZE)
            return EFI_INVALID_PARAMETER;
        status = arosboot_allocate_owned(boot, size, 1U,
            AROS_BOOT_ALLOCATION_FDT,
            AROS_BOOT_ALLOCATION_FLAG_OWNED |
                AROS_BOOT_ALLOCATION_FLAG_RETAIN,
            AROS_BOOT_ALLOCATION_NO_MODULE, &copy);
        if (EFI_ERROR(status))
            return status;
        boot->copy_mem(copy, source, size);
        boot_info->fdt_physical = (uint64_t)(uintptr_t)copy;
        boot_info->fdt_size = size;
        boot_info->flags |= AROS_BOOTINFO_FLAG_FDT;
        return EFI_SUCCESS;
    }
    return EFI_NOT_FOUND;
}

/* Named the way the Apple device tree and Asahi's simple framebuffer name it:
 * ten bits per component with the two high bits ignored.  Any other bitmask
 * stays AROS_BOOTINFO_FB_BITMASK, which every consumer rejects.
 */
static uint32_t bitmask_format(const struct EFI_PIXEL_BITMASK *masks)
{
    if (masks->red_mask == UINT32_C(0x3ff00000) &&
        masks->green_mask == UINT32_C(0x000ffc00) &&
        masks->blue_mask == UINT32_C(0x000003ff) &&
        (masks->reserved_mask == UINT32_C(0xc0000000) ||
         masks->reserved_mask == UINT32_C(0)))
        return AROS_BOOTINFO_FB_X2R10G10B10;
    return AROS_BOOTINFO_FB_BITMASK;
}

static EFI_STATUS capture_framebuffer(
    struct EFI_SYSTEM_TABLE *system, struct AROSBootInfo *boot_info)
{
    struct EFI_GRAPHICS_OUTPUT_PROTOCOL *graphics = NULL;
    struct EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode;
    struct EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
    EFI_STATUS status = system->boot_services->locate_protocol(
        &graphics_output_guid, NULL, (void **)&graphics);

    if (EFI_ERROR(status) || graphics == NULL || graphics->mode == NULL ||
        graphics->mode->info == NULL)
        return EFI_NOT_FOUND;

    mode = graphics->mode;
    info = mode->info;
    if (info->pixel_format == EFI_PIXEL_RED_GREEN_BLUE_RESERVED_8)
        boot_info->framebuffer_format = AROS_BOOTINFO_FB_RGBX8888;
    else if (info->pixel_format == EFI_PIXEL_BLUE_GREEN_RED_RESERVED_8)
        boot_info->framebuffer_format = AROS_BOOTINFO_FB_BGRX8888;
    else if (info->pixel_format == EFI_PIXEL_BIT_MASK)
        boot_info->framebuffer_format = bitmask_format(&info->pixel_information);
    else
        return EFI_NOT_FOUND;

    boot_info->framebuffer_physical = mode->frame_buffer_base;
    boot_info->framebuffer_size = mode->frame_buffer_size;
    boot_info->framebuffer_width = info->horizontal_resolution;
    boot_info->framebuffer_height = info->vertical_resolution;
    boot_info->framebuffer_stride = info->pixels_per_scan_line;
    boot_info->flags |= AROS_BOOTINFO_FLAG_FRAMEBUFFER;
    return EFI_SUCCESS;
}

static EFI_STATUS capture_memory_map_and_exit(
    EFI_HANDLE image, struct EFI_SYSTEM_TABLE *system,
    struct AROSBootInfo *boot_info)
{
    struct EFI_BOOT_SERVICES *boot = system->boot_services;
    EFI_UINTN required = 0;
    EFI_UINTN map_key = 0;
    EFI_UINTN descriptor_size = 0;
    uint32_t descriptor_version = 0;
    struct EFI_MEMORY_DESCRIPTOR *map = NULL;
    EFI_STATUS status;
    unsigned int attempt;

    status = boot->get_memory_map(
        &required, NULL, &map_key, &descriptor_size, &descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL || descriptor_size == 0)
        return status;
    required += descriptor_size * 16U;
    status = arosboot_allocate_owned(boot, required, 1U,
        AROS_BOOT_ALLOCATION_MEMORY_MAP,
        AROS_BOOT_ALLOCATION_FLAG_OWNED |
            AROS_BOOT_ALLOCATION_FLAG_RELEASE_AFTER_STAGE2,
        AROS_BOOT_ALLOCATION_NO_MODULE, (void **)&map);
    if (EFI_ERROR(status))
        return status;

    for (attempt = 0; attempt < MEMORY_MAP_ATTEMPTS; ++attempt) {
        EFI_UINTN capacity = required;

        status = arosboot_publish_allocations(boot, boot_info);
        if (EFI_ERROR(status))
            return status;
        status = boot->get_memory_map(
            &capacity, map, &map_key, &descriptor_size, &descriptor_version);
        if (status == EFI_BUFFER_TOO_SMALL) {
            /*
             * Firmware may grow the map between the sizing query and the real
             * query. Allocate a larger loader-owned buffer and reacquire the
             * map; the old allocation is intentionally left represented in it.
             */
            required = capacity + descriptor_size * 16U;
            status = arosboot_allocate_owned(boot, required, 1U,
                AROS_BOOT_ALLOCATION_MEMORY_MAP,
                AROS_BOOT_ALLOCATION_FLAG_OWNED |
                    AROS_BOOT_ALLOCATION_FLAG_RELEASE_AFTER_STAGE2,
                AROS_BOOT_ALLOCATION_NO_MODULE, (void **)&map);
            if (EFI_ERROR(status))
                return status;
            continue;
        }
        if (EFI_ERROR(status))
            return status;

        boot_info->memory_map_physical = (uint64_t)(uintptr_t)map;
        boot_info->memory_map_size = capacity;
        boot_info->memory_map_entry_size = (uint32_t)descriptor_size;
        boot_info->memory_map_format = AROS_BOOT_MEMORY_MAP_UEFI;
        boot_info->memory_map_version = descriptor_version;
        boot_info->flags |= AROS_BOOTINFO_FLAG_MEMORY_MAP;

        status = boot->exit_boot_services(image, map_key);
        if (status == EFI_SUCCESS)
            return EFI_SUCCESS;
        if (status != EFI_INVALID_PARAMETER)
            return status;
    }
    return EFI_INVALID_PARAMETER;
}

static void draw_post_exit_frame(const struct AROSBootInfo *boot_info)
{
    volatile uint32_t *pixels =
        (volatile uint32_t *)(uintptr_t)boot_info->framebuffer_physical;
    uint32_t width = boot_info->framebuffer_width;
    uint32_t height = boot_info->framebuffer_height;
    uint32_t stride = boot_info->framebuffer_stride;
    uint64_t available_pixels = boot_info->framebuffer_size / sizeof(uint32_t);
    uint32_t x;
    uint32_t y;

    if ((boot_info->flags & AROS_BOOTINFO_FLAG_FRAMEBUFFER) == 0 ||
        pixels == NULL || width == 0 || height == 0 || stride < width ||
        (uint64_t)stride * height > available_pixels)
        return;

    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            uint32_t border = width / 32U + 2U;
            uint32_t color = 0x00102040U;
            if (x < border || y < border || x + border >= width ||
                y + border >= height)
                color = 0x0000d060U;
            else if (y > height / 3U && y < (height * 2U) / 3U &&
                     x > width / 5U && x < (width * 4U) / 5U)
                color = 0x00f0f0f0U;
            pixels[(uint64_t)y * stride + x] = color;
        }
    }
}

#if defined(AROSBOOT_QEMU_TEST)
static void semihost_write0(const char *text)
{
    register uint64_t operation __asm__("x0") = 4;
    register const char *parameter __asm__("x1") = text;
    __asm__ volatile("hlt #0xf000" : "+r"(operation) : "r"(parameter) : "memory");
}

static void post_exit_test(const struct AROSBootInfo *boot_info)
{
    const struct AROSBootModule *modules =
        (const struct AROSBootModule *)(uintptr_t)boot_info->modules_physical;

    if (boot_info->magic == AROS_BOOTINFO_MAGIC &&
        boot_info->version == AROS_BOOTINFO_VERSION &&
        boot_info->size == sizeof(*boot_info) &&
        (boot_info->flags & (AROS_BOOTINFO_FLAG_FDT |
                             AROS_BOOTINFO_FLAG_MEMORY_MAP |
                             AROS_BOOTINFO_FLAG_FRAMEBUFFER |
                             AROS_BOOTINFO_FLAG_MODULES |
                             AROS_BOOTINFO_FLAG_ALLOCATIONS |
                             AROS_BOOTINFO_FLAG_PAYLOAD_IDENTITY)) ==
            (AROS_BOOTINFO_FLAG_FDT | AROS_BOOTINFO_FLAG_MEMORY_MAP |
             AROS_BOOTINFO_FLAG_FRAMEBUFFER | AROS_BOOTINFO_FLAG_MODULES |
             AROS_BOOTINFO_FLAG_ALLOCATIONS |
             AROS_BOOTINFO_FLAG_PAYLOAD_IDENTITY) &&
        boot_info->module_count >= 3 &&
        boot_info->module_entry_size == sizeof(struct AROSBootModule) &&
        boot_info->allocation_count >= 13 &&
        boot_info->allocation_entry_size ==
            sizeof(struct AROSBootAllocation) &&
        boot_info->memory_map_format == AROS_BOOT_MEMORY_MAP_UEFI &&
        boot_info->memory_map_version == AROS_BOOT_MEMORY_MAP_VERSION_1 &&
        boot_info->producer == AROS_BOOT_PRODUCER_UEFI &&
        boot_info->payload_generation_sequence != 0 &&
        boot_info->payload_size != 0 &&
        boot_info->payload_slot <= 1 &&
        (boot_info->payload_flags & AROS_BOOT_PAYLOAD_DIRECT) == 0 &&
        modules != NULL && modules[0].size == TEST_KERNEL_MEMORY_SIZE)
        semihost_write0("[B0] ExitBootServices + BootInfo v4 PASS\n");
    else
        semihost_write0("[B0] post-exit validation did not pass\n");
}
#endif

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, struct EFI_SYSTEM_TABLE *system)
{
    struct AROSBootInfo *boot_info = NULL;
    struct AROSBootBundleResult bundle;
    void *log_copy = NULL;
    void *handoff_stack = NULL;
    EFI_STATUS status;

    open_boot_log(image, system);
    draw_efi_diagnostic(system, 1U, 0, 0);
    print_ascii(system, "AROSBOOTAA64: starting generic UEFI loader\r\n");
    arosboot_allocation_tracker_reset();
    status = arosboot_allocate_owned(system->boot_services,
        sizeof(*boot_info), 16U, AROS_BOOT_ALLOCATION_BOOTINFO,
        AROS_BOOT_ALLOCATION_FLAG_OWNED |
            AROS_BOOT_ALLOCATION_FLAG_RELEASE_AFTER_STAGE2,
        AROS_BOOT_ALLOCATION_NO_MODULE, (void **)&boot_info);
    if (EFI_ERROR(status))
        return stop_with_diagnostic(system, 1U, status);

    boot_info->magic = AROS_BOOTINFO_MAGIC;
    boot_info->version = AROS_BOOTINFO_VERSION;
    boot_info->size = sizeof(*boot_info);
    boot_info->module_entry_size = sizeof(struct AROSBootModule);
    boot_info->producer = AROS_BOOT_PRODUCER_UEFI;

    status = arosboot_allocate_owned(system->boot_services,
        sizeof(loader_log), 1U, AROS_BOOT_ALLOCATION_LOADER_LOG,
        AROS_BOOT_ALLOCATION_FLAG_OWNED |
            AROS_BOOT_ALLOCATION_FLAG_RELEASE_AFTER_STAGE2,
        AROS_BOOT_ALLOCATION_NO_MODULE, &log_copy);
    if (EFI_ERROR(status))
        return stop_with_diagnostic(system, 2U, status);
    system->boot_services->copy_mem(log_copy, loader_log, sizeof(loader_log));
    boot_info->loader_log_physical = (uint64_t)(uintptr_t)log_copy;
    boot_info->loader_log_size = sizeof(loader_log) - 1U;
    boot_info->flags |= AROS_BOOTINFO_FLAG_LOADER_LOG;

    status = arosboot_allocate_owned(system->boot_services,
        HANDOFF_STACK_SIZE, 16U, AROS_BOOT_ALLOCATION_HANDOFF_STACK,
        AROS_BOOT_ALLOCATION_FLAG_OWNED |
            AROS_BOOT_ALLOCATION_FLAG_RELEASE_AFTER_STAGE2,
        AROS_BOOT_ALLOCATION_NO_MODULE, &handoff_stack);
    if (EFI_ERROR(status))
        return stop_with_diagnostic(system, 2U, status);

    draw_efi_diagnostic(system, 2U, 0, 0);

    status = arosboot_load_bundle(image, system, boot_info, &bundle);
    if (EFI_ERROR(status)) {
        print_ascii(system, "AROSBOOTAA64: bundle rejected, status=");
        print_hex64(system, status);
        print_ascii(system, "\r\n");
        return stop_with_diagnostic(system, 3U, status);
    }
    print_ascii(system, "AROSBOOTAA64: bundle verified; modules=");
    print_hex64(system, bundle.module_count);
    print_ascii(system, "\r\n");
    print_ascii(system, "AROSBOOTAA64: generation selected; sequence=");
    print_hex64(system, bundle.generation_sequence);
    print_ascii(system, " slot=");
    print_hex64(system, bundle.generation_slot);
    print_ascii(system, " fallback=");
    print_hex64(system, bundle.generation_fallback);
    print_ascii(system, " degraded=");
    print_hex64(system, bundle.generation_degraded);
    print_ascii(system, "\r\n");
    print_ascii(system, "AROSBOOTAA64: generation transport=");
    print_ascii(system, bundle.generation_memory != 0 ? "ram" : "esp");
    print_ascii(system, "\r\n");
    draw_efi_diagnostic(system, 3U, 0, 0);

    status = copy_fdt(system, boot_info);
    if (status == EFI_NOT_FOUND) {
        extern EFI_STATUS arosboot_vz_platform(struct EFI_SYSTEM_TABLE *, struct AROSBootInfo *);
        status = arosboot_vz_platform(system,boot_info);
        if (!EFI_ERROR(status))
            print_ascii(system,"AROSBOOTAA64: ACPI platform bridge + virtio console prepared\r\n");
    }
    if (EFI_ERROR(status)) {
        print_ascii(system, "AROSBOOTAA64: FDT unavailable, status=");
        print_hex64(system, status);
        print_ascii(system, "\r\n");
        return stop_with_diagnostic(system, 4U, status);
    }
    draw_efi_diagnostic(system, 4U, 0, 0);
    status = capture_framebuffer(system, boot_info);
    if (status == EFI_NOT_FOUND) {
        print_ascii(system,"AROSBOOTAA64: no firmware framebuffer; headless boot\r\n");
    } else if (EFI_ERROR(status)) {
        print_ascii(system, "AROSBOOTAA64: framebuffer unavailable, status=");
        print_hex64(system, status);
        print_ascii(system, "\r\n");
        return stop_with_diagnostic(system, 5U, status);
    }

    draw_efi_diagnostic(system, 5U, 0, 0);

    print_ascii(system, "AROSBOOTAA64: payload entry=");
    print_hex64(system, bundle.kernel_entry);
    print_ascii(system, "\r\n");
    print_ascii(system, "AROSBOOTAA64: platform prepared; exiting boot services\r\n");
    if (boot_log) { boot_log->close(boot_log); boot_log = NULL; }
    status = capture_memory_map_and_exit(image, system, boot_info);
    if (EFI_ERROR(status))
        return stop_with_diagnostic(system, 6U, status);

    draw_post_exit_frame(boot_info);
#if AROSBOOT_FRAME_DIAGNOSTIC
    draw_diagnostic_pattern(
        boot_info->framebuffer_physical, boot_info->framebuffer_size,
        boot_info->framebuffer_width, boot_info->framebuffer_height,
        boot_info->framebuffer_stride, 6U, 0, 0);
#if AROSBOOT_FRAME_DIAGNOSTIC_LATCH
    for (;;)
        __asm__ volatile("wfe");
#endif
#endif
#if defined(AROSBOOT_QEMU_TEST)
    post_exit_test(boot_info);
#endif
    clean_handoff_to_poc(boot_info, handoff_stack);
    arosboot_enter_payload(
        boot_info,
        (uintptr_t)handoff_stack + HANDOFF_STACK_SIZE,
        bundle.kernel_entry);
}
