// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper

#include "bundle_loader.h"
#include "allocation_tracker.h"
#include "sha256.h"
#include <aros/apple/bundle.h>
#include <aros/apple/generation.h>

#define BUNDLE_DIGEST_OFFSET 64U
#define BUNDLE_DIGEST_SIZE 32U

static const struct EFI_GUID loaded_image_guid = {
    0x5b1b31a1, 0x9562, 0x11d2,
    {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};

static const struct EFI_GUID simple_file_system_guid = {
    0x964e5b22, 0x6459, 0x11d2,
    {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};

static const struct EFI_GUID file_info_guid = {
    0x09576e92, 0x6d3f, 0x11d2,
    {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};

static const struct EFI_GUID load_file2_guid = {
    0x4006c0c1, 0xfcb3, 0x403e,
    {0x99, 0x6d, 0x4a, 0x6c, 0x87, 0x24, 0xe0, 0x6d}
};

/* Linux's published vendor-media device path for the EFI initrd service. */
static const struct EFI_INITRD_DEVICE_PATH initrd_device_path = {
    .vendor = {
        .header = {
            EFI_DEVICE_PATH_TYPE_MEDIA,
            EFI_DEVICE_PATH_SUBTYPE_VENDOR,
            {sizeof(struct EFI_VENDOR_MEDIA_DEVICE_PATH), 0}
        },
        .vendor_guid = {
            0x5568e427, 0x68fc, 0x4f3d,
            {0xac, 0x74, 0xca, 0x55, 0x52, 0x31, 0xcc, 0x68}
        }
    },
    .end = {
        EFI_DEVICE_PATH_TYPE_END,
        EFI_DEVICE_PATH_SUBTYPE_END,
        {sizeof(struct EFI_DEVICE_PATH_PROTOCOL), 0}
    }
};

static EFI_STATUS load_file(
    EFI_HANDLE image, struct EFI_SYSTEM_TABLE *system,
    EFI_CHAR16 *path, size_t minimum, size_t maximum, int *opened,
    uint8_t **bytes, size_t *byte_count)
{
    struct EFI_BOOT_SERVICES *boot = system->boot_services;
    struct EFI_LOADED_IMAGE_PROTOCOL *loaded = NULL;
    struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *filesystem = NULL;
    struct EFI_FILE_PROTOCOL *root = NULL;
    struct EFI_FILE_PROTOCOL *file = NULL;
    struct EFI_FILE_INFO_PREFIX *info = NULL;
    EFI_UINTN info_size = 0;
    EFI_UINTN read_size;
    size_t allocated_info_size;
    EFI_STATUS status;

    *opened = 0;
    status = boot->handle_protocol(
        image, &loaded_image_guid, (void **)&loaded);
    if (EFI_ERROR(status) || loaded == NULL || loaded->device_handle == NULL)
        return EFI_NOT_FOUND;
    status = boot->handle_protocol(
        loaded->device_handle, &simple_file_system_guid, (void **)&filesystem);
    if (EFI_ERROR(status) || filesystem == NULL)
        return EFI_NOT_FOUND;
    status = filesystem->open_volume(filesystem, &root);
    if (EFI_ERROR(status))
        return status;
    if (root == NULL)
        return EFI_LOAD_ERROR;
    status = root->open(root, &file, path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status) || file == NULL) {
        root->close(root);
        return EFI_ERROR(status) ? status : EFI_LOAD_ERROR;
    }
    *opened = 1;

    status = file->get_info(file, &file_info_guid, &info_size, NULL);
    if (status != EFI_BUFFER_TOO_SMALL || info_size < sizeof(*info)) {
        file->close(file);
        root->close(root);
        return EFI_LOAD_ERROR;
    }
    allocated_info_size = info_size;
    status = arosboot_allocate_owned(boot, allocated_info_size, 1U, 0, 0,
        AROS_BOOT_ALLOCATION_NO_MODULE, (void **)&info);
    if (!EFI_ERROR(status))
        status = file->get_info(file, &file_info_guid, &info_size, info);
    if (EFI_ERROR(status) || info_size < sizeof(*info) ||
        info->size < sizeof(*info) ||
        info->file_size < minimum || info->file_size > maximum ||
        info->file_size > SIZE_MAX) {
        arosboot_free_owned(boot, info);
        file->close(file);
        root->close(root);
        return EFI_LOAD_ERROR;
    }

    *byte_count = (size_t)info->file_size;
    arosboot_free_owned(boot, info);
    status = arosboot_allocate_owned(boot, *byte_count, 1U, 0, 0,
        AROS_BOOT_ALLOCATION_NO_MODULE, (void **)bytes);
    if (!EFI_ERROR(status)) {
        read_size = *byte_count;
        status = file->read(file, &read_size, *bytes);
        if (!EFI_ERROR(status) && read_size != *byte_count)
            status = EFI_LOAD_ERROR;
    }
    file->close(file);
    root->close(root);
    if (EFI_ERROR(status)) {
        arosboot_free_owned(boot, *bytes);
        *bytes = NULL;
        *byte_count = 0;
    }
    return status;
}

static int bytes_equal(const uint8_t *left, const uint8_t *right, size_t size)
{
    size_t index;
    uint8_t difference = 0;

    for (index = 0; index < size; ++index)
        difference |= left[index] ^ right[index];
    return difference == 0;
}

static int words_are_zero(const uint64_t *words, size_t count)
{
    size_t index;
    uint64_t combined = 0;

    for (index = 0; index < count; ++index)
        combined |= words[index];
    return combined == 0;
}

static int range_valid(uint64_t offset, uint64_t size, uint64_t total)
{
    return offset <= total && size <= total - offset;
}

static int ranges_overlap(
    uint64_t first_offset, uint64_t first_size,
    uint64_t second_offset, uint64_t second_size)
{
    return first_offset < second_offset + second_size &&
           second_offset < first_offset + first_size;
}

static int device_path_is_end(const struct EFI_DEVICE_PATH_PROTOCOL *path)
{
    return path->type == EFI_DEVICE_PATH_TYPE_END &&
           path->subtype == EFI_DEVICE_PATH_SUBTYPE_END &&
           path->length[0] == sizeof(*path) && path->length[1] == 0;
}

static int valid_utf8_name(const uint8_t *text, size_t size)
{
    size_t index = 0;

    if (size == 0)
        return 0;
    while (index < size) {
        uint8_t first = text[index++];
        uint32_t codepoint;
        unsigned int continuation;

        if (first == 0)
            return 0;
        if (first < 0x80)
            continue;
        if (first >= 0xc2 && first <= 0xdf) {
            codepoint = first & 0x1fU;
            continuation = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            codepoint = first & 0x0fU;
            continuation = 2;
        } else if (first >= 0xf0 && first <= 0xf4) {
            codepoint = first & 0x07U;
            continuation = 3;
        } else {
            return 0;
        }
        if (continuation > size - index)
            return 0;
        while (continuation-- != 0) {
            uint8_t next = text[index++];
            if ((next & 0xc0U) != 0x80U)
                return 0;
            codepoint = (codepoint << 6) | (next & 0x3fU);
        }
        if ((codepoint < 0x800U && first >= 0xe0) ||
            (codepoint < 0x10000U && first >= 0xf0) ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU) ||
            codepoint > 0x10ffffU)
            return 0;
    }
    return 1;
}

static void digest_bundle(const uint8_t *bytes, size_t size, uint8_t digest[32])
{
    static const uint8_t zero_digest[BUNDLE_DIGEST_SIZE] = {0};
    struct AROSBootSHA256 context;

    AROSBootSHA256Init(&context);
    AROSBootSHA256Update(&context, bytes, BUNDLE_DIGEST_OFFSET);
    AROSBootSHA256Update(&context, zero_digest, sizeof(zero_digest));
    AROSBootSHA256Update(
        &context, bytes + BUNDLE_DIGEST_OFFSET + BUNDLE_DIGEST_SIZE,
        size - BUNDLE_DIGEST_OFFSET - BUNDLE_DIGEST_SIZE);
    AROSBootSHA256Final(&context, digest);
}

static void digest_generation_record(
    const struct AROSBootGenerationRecord *record, uint8_t digest[32])
{
    static const uint8_t zero_digest[AROS_BOOT_GENERATION_DIGEST_BYTES] = {0};
    const uint8_t *bytes = (const uint8_t *)record;
    struct AROSBootSHA256 context;

    AROSBootSHA256Init(&context);
    AROSBootSHA256Update(
        &context, bytes, AROS_BOOT_GENERATION_DIGEST_OFFSET);
    AROSBootSHA256Update(&context, zero_digest, sizeof(zero_digest));
    AROSBootSHA256Update(
        &context,
        bytes + AROS_BOOT_GENERATION_DIGEST_OFFSET +
            AROS_BOOT_GENERATION_DIGEST_BYTES,
        sizeof(*record) - AROS_BOOT_GENERATION_DIGEST_OFFSET -
            AROS_BOOT_GENERATION_DIGEST_BYTES);
    AROSBootSHA256Final(&context, digest);
}

static int valid_generation_record(
    const struct AROSBootGenerationRecord *record, uint32_t expected_slot)
{
    uint8_t digest[32];

    if (record->magic != AROS_BOOT_GENERATION_MAGIC ||
        record->version != AROS_BOOT_GENERATION_VERSION ||
        record->size != AROS_BOOT_GENERATION_BYTES ||
        record->sequence == 0 || record->slot != expected_slot ||
        (record->flags != AROS_BOOT_GENERATION_ACCEPTED &&
         record->flags != AROS_BOOT_GENERATION_CANDIDATE) ||
        record->bundle_size < sizeof(struct AROSBundleHeader) ||
        record->bundle_size > AROS_BUNDLE_MAX_BYTES ||
        record->loader_abi_min == 0 ||
        record->loader_abi_min > record->loader_abi_max ||
        AROS_BOOT_GENERATION_LOADER_ABI < record->loader_abi_min ||
        AROS_BOOT_GENERATION_LOADER_ABI > record->loader_abi_max ||
        !words_are_zero(record->reserved, 3))
        return 0;
    digest_generation_record(record, digest);
    return bytes_equal(digest, record->record_sha256, sizeof(digest));
}

static EFI_STATUS load_generation_record(
    EFI_HANDLE image, struct EFI_SYSTEM_TABLE *system,
    EFI_CHAR16 *path, uint32_t expected_slot,
    struct AROSBootGenerationRecord *record, int *opened)
{
    uint8_t *bytes = NULL;
    size_t byte_count = 0;
    EFI_STATUS status = load_file(
        image, system, path, sizeof(*record), sizeof(*record), opened,
        &bytes, &byte_count);

    if (EFI_ERROR(status))
        return status;
    system->boot_services->copy_mem(record, bytes, sizeof(*record));
    arosboot_free_owned(system->boot_services, bytes);
    if (!valid_generation_record(record, expected_slot))
        return EFI_SECURITY_VIOLATION;
    return EFI_SUCCESS;
}

static EFI_STATUS load_initrd(
    EFI_HANDLE image, struct EFI_SYSTEM_TABLE *system,
    uint8_t **bytes, size_t *byte_count)
{
    struct EFI_BOOT_SERVICES *boot = system->boot_services;
    struct EFI_DEVICE_PATH_PROTOCOL *path =
        (struct EFI_DEVICE_PATH_PROTOCOL *)(uintptr_t)&initrd_device_path;
    struct EFI_LOAD_FILE2_PROTOCOL *load_file2 = NULL;
    EFI_HANDLE handle = NULL;
    EFI_UINTN size = 0;
    EFI_UINTN capacity;
    EFI_STATUS status;

    *bytes = NULL;
    *byte_count = 0;
    status = boot->locate_device_path(&load_file2_guid, &path, &handle);
    if (EFI_ERROR(status))
        return status;
    if (handle == NULL || path == NULL || !device_path_is_end(path))
        return EFI_NOT_FOUND;
    status = boot->open_protocol(
        handle, &load_file2_guid, (void **)&load_file2,
        image, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR(status))
        return status;
    if (load_file2 == NULL || load_file2->load_file == NULL)
        return EFI_LOAD_ERROR;

    status = load_file2->load_file(load_file2, path, 0, &size, NULL);
    if (status == EFI_NOT_FOUND)
        return EFI_NOT_FOUND;
    if (status != EFI_BUFFER_TOO_SMALL)
        return EFI_LOAD_ERROR;
    if (size < sizeof(struct AROSBootGenerationRecord) +
                   sizeof(struct AROSBundleHeader) ||
        size > sizeof(struct AROSBootGenerationRecord) +
                   AROS_BUNDLE_MAX_BYTES ||
        size > SIZE_MAX)
        return EFI_SECURITY_VIOLATION;

    capacity = size;
    status = arosboot_allocate_owned(boot, (size_t)capacity, 16U, 0, 0,
        AROS_BOOT_ALLOCATION_NO_MODULE, (void **)bytes);
    if (EFI_ERROR(status))
        return status;
    status = load_file2->load_file(load_file2, path, 0, &size, *bytes);
    if (EFI_ERROR(status) || size != capacity) {
        arosboot_free_owned(boot, *bytes);
        *bytes = NULL;
        return EFI_ERROR(status) ? status : EFI_LOAD_ERROR;
    }
    *byte_count = (size_t)size;
    return EFI_SUCCESS;
}

static int validate_component(
    const uint8_t *bytes, uint64_t total,
    const struct AROSBundleHeader *header,
    const struct AROSBundleComponent *component, uint32_t index,
    uint32_t *kernel_count)
{
    const struct AROSBundleComponent *other;
    const uint8_t *name;
    uint8_t digest[32];
    uint32_t other_index;

    if (!words_are_zero(component->reserved, 4) ||
        component->data_size == 0 ||
        component->memory_size < component->data_size ||
        component->memory_size > AROS_BUNDLE_MAX_COMPONENT_MEMORY ||
        component->alignment < 16 ||
        component->alignment > AROS_BUNDLE_MAX_ALIGNMENT ||
        (component->alignment & (component->alignment - 1U)) != 0 ||
        !range_valid(component->data_offset, component->data_size, total) ||
        component->data_offset < header->strings_offset + header->strings_size ||
        (component->data_offset & (component->alignment - 1U)) != 0 ||
        !range_valid(component->name_offset, component->name_size,
                     header->strings_size) ||
        (component->flags & ~(AROS_BUNDLE_FLAG_EXECUTABLE |
                              AROS_BUNDLE_FLAG_REQUIRED)) != 0)
        return 0;

    for (other_index = 0; other_index < index; ++other_index) {
        other = (const struct AROSBundleComponent *)(
            bytes + header->components_offset +
            (uint64_t)other_index * header->component_entry_size);
        if (ranges_overlap(component->data_offset, component->data_size,
                           other->data_offset, other->data_size))
            return 0;
    }

    name = bytes + header->strings_offset + component->name_offset;
    if (!valid_utf8_name(name, component->name_size))
        return 0;

    AROSBootSHA256(bytes + component->data_offset,
                   (size_t)component->data_size, digest);
    if (!bytes_equal(digest, component->sha256, sizeof(digest)))
        return 0;

    if (component->type == AROS_BUNDLE_KERNEL) {
        ++*kernel_count;
        if (component->format != AROS_BUNDLE_FORMAT_RAW_PI ||
            (component->flags & AROS_BUNDLE_FLAG_EXECUTABLE) == 0 ||
            component->entry_offset >= component->data_size)
            return 0;
    } else if (component->type == AROS_BUNDLE_COMPONENT ||
               component->type == AROS_BUNDLE_RAMDISK) {
        if (component->format != AROS_BUNDLE_FORMAT_DATA ||
            (component->flags & AROS_BUNDLE_FLAG_EXECUTABLE) != 0 ||
            component->entry_offset != 0)
            return 0;
    } else {
        return 0;
    }
    return 1;
}

static EFI_STATUS validate_bundle(
    const uint8_t *bytes, size_t byte_count,
    const struct AROSBundleHeader **header_result)
{
    const struct AROSBundleHeader *header;
    const struct AROSBundleComponent *component;
    uint8_t digest[32];
    uint64_t table_size;
    uint32_t index;
    uint32_t kernel_count = 0;

    if (byte_count < sizeof(*header) || byte_count > AROS_BUNDLE_MAX_BYTES)
        return EFI_SECURITY_VIOLATION;
    header = (const struct AROSBundleHeader *)bytes;
    if (header->magic != AROS_BUNDLE_MAGIC ||
        header->version != AROS_BUNDLE_VERSION ||
        header->header_size != sizeof(*header) ||
        header->total_size != byte_count || header->flags != 0 ||
        !words_are_zero(header->reserved, 4) ||
        header->component_count == 0 ||
        header->component_count > AROS_BUNDLE_MAX_COMPONENTS ||
        header->component_entry_size != sizeof(*component))
        return EFI_SECURITY_VIOLATION;

    table_size = (uint64_t)header->component_count *
                 header->component_entry_size;
    if (!range_valid(header->components_offset, table_size, byte_count) ||
        !range_valid(header->strings_offset, header->strings_size, byte_count) ||
        header->components_offset < sizeof(*header) ||
        (header->components_offset & 15U) != 0 ||
        header->strings_offset < header->components_offset + table_size)
        return EFI_SECURITY_VIOLATION;

    digest_bundle(bytes, byte_count, digest);
    if (!bytes_equal(digest, header->sha256, sizeof(digest)))
        return EFI_SECURITY_VIOLATION;
    for (index = 0; index < header->component_count; ++index) {
        component = (const struct AROSBundleComponent *)(
            bytes + header->components_offset +
            (uint64_t)index * header->component_entry_size);
        if (!validate_component(
                bytes, byte_count, header, component, index, &kernel_count))
            return EFI_SECURITY_VIOLATION;
    }
    if (kernel_count != 1)
        return EFI_SECURITY_VIOLATION;
    *header_result = header;
    return EFI_SUCCESS;
}

static EFI_STATUS materialize_bundle(
    struct EFI_SYSTEM_TABLE *system, const uint8_t *bytes,
    const struct AROSBundleHeader *header, struct AROSBootInfo *boot_info,
    struct AROSBootBundleResult *result)
{
    struct EFI_BOOT_SERVICES *boot = system->boot_services;
    struct AROSBootModule *modules;
    const struct AROSBundleComponent *component;
    void *loaded;
    void *name;
    EFI_STATUS status;
    uint32_t allocation_flags;
    uint32_t index;

    status = arosboot_allocate_owned(boot,
        (size_t)header->component_count * sizeof(*modules), 16U,
        AROS_BOOT_ALLOCATION_MODULE_TABLE,
        AROS_BOOT_ALLOCATION_FLAG_OWNED |
            AROS_BOOT_ALLOCATION_FLAG_RELEASE_AFTER_STAGE2,
        AROS_BOOT_ALLOCATION_NO_MODULE, (void **)&modules);
    if (EFI_ERROR(status))
        return status;

    for (index = 0; index < header->component_count; ++index) {
        component = (const struct AROSBundleComponent *)(
            bytes + header->components_offset +
            (uint64_t)index * header->component_entry_size);
        allocation_flags = AROS_BOOT_ALLOCATION_FLAG_OWNED;
        if (component->type == AROS_BUNDLE_KERNEL ||
            component->type == AROS_BUNDLE_RAMDISK)
            allocation_flags |= AROS_BOOT_ALLOCATION_FLAG_RETAIN;
        else
            allocation_flags |=
                AROS_BOOT_ALLOCATION_FLAG_RELEASE_AFTER_STAGE2;
        status = arosboot_allocate_owned(boot,
            (size_t)component->memory_size, (size_t)component->alignment,
            AROS_BOOT_ALLOCATION_MODULE_DATA, allocation_flags,
            index, &loaded);
        if (EFI_ERROR(status))
            return status;
        boot->copy_mem(
            loaded, bytes + component->data_offset,
            (size_t)component->data_size);
        status = arosboot_allocate_owned(boot, component->name_size, 1U,
            AROS_BOOT_ALLOCATION_MODULE_NAME,
            AROS_BOOT_ALLOCATION_FLAG_OWNED |
                AROS_BOOT_ALLOCATION_FLAG_RELEASE_AFTER_STAGE2,
            index, &name);
        if (EFI_ERROR(status))
            return status;
        boot->copy_mem(
            name, bytes + header->strings_offset + component->name_offset,
            component->name_size);

        modules[index].physical_address = (uint64_t)(uintptr_t)loaded;
        modules[index].size = component->memory_size;
        modules[index].name_physical = (uint64_t)(uintptr_t)name;
        modules[index].name_size = component->name_size;
        modules[index].type = component->type;
        if (component->type == AROS_BUNDLE_KERNEL)
            result->kernel_entry = (uintptr_t)loaded + component->entry_offset;
    }

    boot_info->modules_physical = (uint64_t)(uintptr_t)modules;
    boot_info->module_count = header->component_count;
    boot_info->module_entry_size = sizeof(*modules);
    boot_info->flags |= AROS_BOOTINFO_FLAG_MODULES;
    result->module_count = header->component_count;
    return EFI_SUCCESS;
}

static EFI_STATUS load_memory_generation(
    EFI_HANDLE image, struct EFI_SYSTEM_TABLE *system,
    struct AROSBootInfo *boot_info, struct AROSBootBundleResult *result)
{
    struct AROSBootGenerationRecord *record;
    const struct AROSBundleHeader *header = NULL;
    uint8_t *generation = NULL;
    const uint8_t *bundle;
    size_t generation_size = 0;
    size_t bundle_size;
    uint8_t bundle_digest[32];
    EFI_STATUS status;

    status = load_initrd(image, system, &generation, &generation_size);
    if (EFI_ERROR(status))
        return status;

    record = (struct AROSBootGenerationRecord *)generation;
    bundle = generation + sizeof(*record);
    bundle_size = generation_size - sizeof(*record);
    if (record->slot > 1 || !valid_generation_record(record, record->slot) ||
        record->bundle_size != bundle_size) {
        status = EFI_SECURITY_VIOLATION;
        goto out;
    }
    AROSBootSHA256(bundle, bundle_size, bundle_digest);
    if (!bytes_equal(bundle_digest, record->bundle_sha256,
                     sizeof(bundle_digest))) {
        status = EFI_SECURITY_VIOLATION;
        goto out;
    }
    status = validate_bundle(bundle, bundle_size, &header);
    if (EFI_ERROR(status))
        goto out;
    status = materialize_bundle(system, bundle, header, boot_info, result);
    if (EFI_ERROR(status))
        goto out;

    boot_info->payload_generation_sequence = record->sequence;
    boot_info->payload_size = bundle_size;
    system->boot_services->copy_mem(
        boot_info->payload_sha256, bundle_digest,
        sizeof(boot_info->payload_sha256));
    boot_info->payload_slot = record->slot;
    boot_info->payload_flags = 0;
    boot_info->flags |= AROS_BOOTINFO_FLAG_PAYLOAD_IDENTITY;
    result->generation_sequence = record->sequence;
    result->generation_slot = record->slot;
    result->generation_fallback = 0;
    result->generation_degraded = 0;
    result->generation_memory = 1;

out:
    arosboot_free_owned(system->boot_services, generation);
    return status;
}

EFI_STATUS arosboot_load_bundle(
    EFI_HANDLE image, struct EFI_SYSTEM_TABLE *system,
    struct AROSBootInfo *boot_info, struct AROSBootBundleResult *result)
{
    static EFI_CHAR16 record_paths[2][15] = {
        {'\\', 'A', 'R', 'O', 'S', '\\', 'B', 'O', 'O', 'T', '0', '.', 'I', 'D', 0},
        {'\\', 'A', 'R', 'O', 'S', '\\', 'B', 'O', 'O', 'T', '1', '.', 'I', 'D', 0},
    };
    static EFI_CHAR16 bundle_paths[2][15] = {
        {'\\', 'A', 'R', 'O', 'S', '\\', 'G', 'E', 'N', '0', '.', 'B', 'N', 'D', 0},
        {'\\', 'A', 'R', 'O', 'S', '\\', 'G', 'E', 'N', '1', '.', 'B', 'N', 'D', 0},
    };
    struct AROSBootGenerationRecord records[2];
    uint32_t order[2] = {0, 1};
    int valid[2] = {0, 0};
    int degraded = 0;
    uint8_t *bytes = NULL;
    size_t byte_count = 0;
    const struct AROSBundleHeader *header = NULL;
    uint8_t bundle_digest[32];
    EFI_STATUS status;
    uint32_t index;
    uint32_t attempt;

    result->kernel_entry = 0;
    result->module_count = 0;
    result->generation_sequence = 0;
    result->generation_slot = 0;
    result->generation_fallback = 0;
    result->generation_degraded = 0;
    result->generation_memory = 0;

    status = load_memory_generation(image, system, boot_info, result);
    if (!EFI_ERROR(status))
        return EFI_SUCCESS;
    if (status != EFI_NOT_FOUND)
        return status;

    for (index = 0; index < 2; ++index) {
        int opened = 0;

        status = load_generation_record(
            image, system, record_paths[index], index, &records[index], &opened);
        if (!EFI_ERROR(status))
            valid[index] = 1;
        else if (opened)
            degraded = 1;
    }
    if (!valid[0] && !valid[1])
        return degraded ? EFI_SECURITY_VIOLATION : EFI_NOT_FOUND;
    if (valid[0] && valid[1] && records[0].sequence == records[1].sequence)
        return EFI_SECURITY_VIOLATION;
    if (!valid[0] ||
        (valid[1] && records[1].sequence > records[0].sequence)) {
        order[0] = 1;
        order[1] = 0;
    }

    for (attempt = 0; attempt < 2; ++attempt) {
        int opened = 0;
        uint32_t slot = order[attempt];

        if (!valid[slot])
            continue;
        status = load_file(
            image, system, bundle_paths[slot], sizeof(struct AROSBundleHeader),
            AROS_BUNDLE_MAX_BYTES, &opened, &bytes, &byte_count);
        if (EFI_ERROR(status)) {
            degraded = 1;
            continue;
        }
        AROSBootSHA256(bytes, byte_count, bundle_digest);
        if (byte_count != records[slot].bundle_size ||
            !bytes_equal(bundle_digest, records[slot].bundle_sha256,
                         sizeof(bundle_digest)) ||
            EFI_ERROR(validate_bundle(bytes, byte_count, &header))) {
            arosboot_free_owned(system->boot_services, bytes);
            bytes = NULL;
            degraded = 1;
            continue;
        }
        status = materialize_bundle(system, bytes, header, boot_info, result);
        if (!EFI_ERROR(status)) {
            boot_info->payload_generation_sequence = records[slot].sequence;
            boot_info->payload_size = byte_count;
            system->boot_services->copy_mem(
                boot_info->payload_sha256, bundle_digest,
                sizeof(boot_info->payload_sha256));
            boot_info->payload_slot = slot;
            boot_info->payload_flags =
                (attempt != 0 ? AROS_BOOT_PAYLOAD_FALLBACK : 0) |
                (degraded ? AROS_BOOT_PAYLOAD_DEGRADED : 0);
            boot_info->flags |= AROS_BOOTINFO_FLAG_PAYLOAD_IDENTITY;
        }
        arosboot_free_owned(system->boot_services, bytes);
        if (EFI_ERROR(status))
            return status;
        result->generation_sequence = records[slot].sequence;
        result->generation_slot = slot;
        result->generation_fallback = attempt != 0;
        result->generation_degraded = degraded;
        return EFI_SUCCESS;
    }
    return EFI_SECURITY_VIOLATION;
}
