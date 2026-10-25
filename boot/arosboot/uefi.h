// SPDX-License-Identifier: MIT
// Copyright (c) 2026 John Knipper
// Minimal UEFI 2.11 declarations authored from the published UEFI specification.

#ifndef AROSBOOT_UEFI_H
#define AROSBOOT_UEFI_H

#include <stddef.h>
#include <stdint.h>

#define EFIAPI

typedef uint64_t EFI_STATUS;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef size_t EFI_UINTN;
typedef void *EFI_HANDLE;
typedef uint16_t EFI_CHAR16;
typedef uint8_t EFI_BOOLEAN;

#define EFI_SUCCESS             UINT64_C(0)
#define EFI_LOAD_ERROR          UINT64_C(0x8000000000000001)
#define EFI_INVALID_PARAMETER   UINT64_C(0x8000000000000002)
#define EFI_BUFFER_TOO_SMALL    UINT64_C(0x8000000000000005)
#define EFI_OUT_OF_RESOURCES    UINT64_C(0x8000000000000009)
#define EFI_NOT_FOUND           UINT64_C(0x800000000000000e)
#define EFI_SECURITY_VIOLATION  UINT64_C(0x800000000000001a)
#define EFI_ERROR(status)       (((status) & (UINT64_C(1) << 63)) != 0)

#define EFI_ALLOCATE_ANY_PAGES  0U
#define EFI_LOADER_DATA         2U
#define EFI_PAGE_SIZE           4096U
#define EFI_OPEN_PROTOCOL_GET_PROTOCOL UINT32_C(0x00000002)

#define EFI_DEVICE_PATH_TYPE_MEDIA UINT8_C(0x04)
#define EFI_DEVICE_PATH_SUBTYPE_VENDOR UINT8_C(0x03)
#define EFI_DEVICE_PATH_TYPE_END UINT8_C(0x7f)
#define EFI_DEVICE_PATH_SUBTYPE_END UINT8_C(0xff)

struct EFI_GUID {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

struct EFI_DEVICE_PATH_PROTOCOL {
    uint8_t type;
    uint8_t subtype;
    uint8_t length[2];
} __attribute__((packed));

struct EFI_VENDOR_MEDIA_DEVICE_PATH {
    struct EFI_DEVICE_PATH_PROTOCOL header;
    struct EFI_GUID vendor_guid;
} __attribute__((packed));

struct EFI_INITRD_DEVICE_PATH {
    struct EFI_VENDOR_MEDIA_DEVICE_PATH vendor;
    struct EFI_DEVICE_PATH_PROTOCOL end;
} __attribute__((packed));

struct EFI_TABLE_HEADER {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
};

struct EFI_TIME {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t pad1;
    uint32_t nanosecond;
    int16_t time_zone;      /* minutes; 2047 when unspecified */
    uint8_t daylight;
    uint8_t pad2;
};

/* Only GetTime is used, before ExitBootServices. */
struct EFI_RUNTIME_SERVICES {
    struct EFI_TABLE_HEADER header;
    EFI_STATUS (*get_time)(struct EFI_TIME *time, void *capabilities);
};

struct EFI_MEMORY_DESCRIPTOR {
    uint32_t type;
    uint32_t padding;
    EFI_PHYSICAL_ADDRESS physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
};

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef EFI_STATUS(EFIAPI *EFI_TEXT_RESET)(
    struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, uint8_t);
typedef EFI_STATUS(EFIAPI *EFI_TEXT_OUTPUT_STRING)(
    struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, EFI_CHAR16 *);

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_TEXT_RESET reset;
    EFI_TEXT_OUTPUT_STRING output_string;
    void *test_string;
    void *query_mode;
    void *set_mode;
    void *set_attribute;
    void *clear_screen;
    void *set_cursor_position;
    void *enable_cursor;
    void *mode;
};

struct EFI_CONFIGURATION_TABLE {
    struct EFI_GUID vendor_guid;
    void *vendor_table;
};

typedef EFI_STATUS(EFIAPI *EFI_ALLOCATE_PAGES)(
    uint32_t, uint32_t, EFI_UINTN, EFI_PHYSICAL_ADDRESS *);
typedef EFI_STATUS(EFIAPI *EFI_FREE_PAGES)(
    EFI_PHYSICAL_ADDRESS, EFI_UINTN);
typedef EFI_STATUS(EFIAPI *EFI_GET_MEMORY_MAP)(
    EFI_UINTN *, struct EFI_MEMORY_DESCRIPTOR *, EFI_UINTN *, EFI_UINTN *,
    uint32_t *);
typedef EFI_STATUS(EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE, EFI_UINTN);
typedef EFI_STATUS(EFIAPI *EFI_HANDLE_PROTOCOL)(
    EFI_HANDLE, const struct EFI_GUID *, void **);
typedef EFI_STATUS(EFIAPI *EFI_LOCATE_PROTOCOL)(
    const struct EFI_GUID *, void *, void **);
typedef EFI_STATUS(EFIAPI *EFI_LOCATE_DEVICE_PATH)(
    const struct EFI_GUID *, struct EFI_DEVICE_PATH_PROTOCOL **, EFI_HANDLE *);
typedef EFI_STATUS(EFIAPI *EFI_OPEN_PROTOCOL)(
    EFI_HANDLE, const struct EFI_GUID *, void **, EFI_HANDLE, EFI_HANDLE,
    uint32_t);
typedef EFI_STATUS(EFIAPI *EFI_STALL)(EFI_UINTN);
typedef void(EFIAPI *EFI_COPY_MEM)(void *, const void *, EFI_UINTN);
typedef void(EFIAPI *EFI_SET_MEM)(void *, EFI_UINTN, uint8_t);

/*
 * The field order is the stable UEFI boot-services ABI. Unused services remain
 * pointer-sized slots so the functions we call retain their specified offsets.
 */
struct EFI_BOOT_SERVICES {
    struct EFI_TABLE_HEADER header;
    void *raise_tpl;
    void *restore_tpl;
    EFI_ALLOCATE_PAGES allocate_pages;
    EFI_FREE_PAGES free_pages;
    EFI_GET_MEMORY_MAP get_memory_map;
    void *allocate_pool;
    void *free_pool;
    void *create_event;
    void *set_timer;
    void *wait_for_event;
    void *signal_event;
    void *close_event;
    void *check_event;
    void *install_protocol_interface;
    void *reinstall_protocol_interface;
    void *uninstall_protocol_interface;
    EFI_HANDLE_PROTOCOL handle_protocol;
    void *reserved;
    void *register_protocol_notify;
    void *locate_handle;
    EFI_LOCATE_DEVICE_PATH locate_device_path;
    void *install_configuration_table;
    void *load_image;
    void *start_image;
    void *exit;
    void *unload_image;
    EFI_EXIT_BOOT_SERVICES exit_boot_services;
    void *get_next_monotonic_count;
    EFI_STALL stall;
    void *set_watchdog_timer;
    void *connect_controller;
    void *disconnect_controller;
    EFI_OPEN_PROTOCOL open_protocol;
    void *close_protocol;
    void *open_protocol_information;
    void *protocols_per_handle;
    void *locate_handle_buffer;
    EFI_LOCATE_PROTOCOL locate_protocol;
    void *install_multiple_protocol_interfaces;
    void *uninstall_multiple_protocol_interfaces;
    void *calculate_crc32;
    EFI_COPY_MEM copy_mem;
    EFI_SET_MEM set_mem;
    void *create_event_ex;
};

struct EFI_SYSTEM_TABLE {
    struct EFI_TABLE_HEADER header;
    EFI_CHAR16 *firmware_vendor;
    uint32_t firmware_revision;
    uint32_t padding;
    EFI_HANDLE console_in_handle;
    void *console_in;
    EFI_HANDLE console_out_handle;
    struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *console_out;
    EFI_HANDLE standard_error_handle;
    struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *standard_error;
    void *runtime_services;
    struct EFI_BOOT_SERVICES *boot_services;
    EFI_UINTN number_of_table_entries;
    struct EFI_CONFIGURATION_TABLE *configuration_table;
};

enum EFI_GRAPHICS_PIXEL_FORMAT {
    EFI_PIXEL_RED_GREEN_BLUE_RESERVED_8 = 0,
    EFI_PIXEL_BLUE_GREEN_RED_RESERVED_8 = 1,
    EFI_PIXEL_BIT_MASK = 2,
    EFI_PIXEL_BLT_ONLY = 3,
    EFI_PIXEL_FORMAT_MAX = 4
};

struct EFI_PIXEL_BITMASK {
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved_mask;
};

struct EFI_GRAPHICS_OUTPUT_MODE_INFORMATION {
    uint32_t version;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    enum EFI_GRAPHICS_PIXEL_FORMAT pixel_format;
    struct EFI_PIXEL_BITMASK pixel_information;
    uint32_t pixels_per_scan_line;
};

struct EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE {
    uint32_t max_mode;
    uint32_t mode;
    struct EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
    EFI_UINTN size_of_info;
    EFI_PHYSICAL_ADDRESS frame_buffer_base;
    EFI_UINTN frame_buffer_size;
};

struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    void *query_mode;
    void *set_mode;
    void *blt;
    struct EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode;
};

struct EFI_LOADED_IMAGE_PROTOCOL {
    uint32_t revision;
    uint32_t padding0;
    EFI_HANDLE parent_handle;
    struct EFI_SYSTEM_TABLE *system_table;
    EFI_HANDLE device_handle;
    void *file_path;
    void *reserved;
    uint32_t load_options_size;
    uint32_t padding1;
    void *load_options;
    void *image_base;
    uint64_t image_size;
    uint32_t image_code_type;
    uint32_t image_data_type;
    void *unload;
};

struct EFI_FILE_PROTOCOL;
typedef EFI_STATUS(EFIAPI *EFI_FILE_OPEN)(
    struct EFI_FILE_PROTOCOL *, struct EFI_FILE_PROTOCOL **,
    EFI_CHAR16 *, uint64_t, uint64_t);
typedef EFI_STATUS(EFIAPI *EFI_FILE_CLOSE)(struct EFI_FILE_PROTOCOL *);
typedef EFI_STATUS(EFIAPI *EFI_FILE_READ)(
    struct EFI_FILE_PROTOCOL *, EFI_UINTN *, void *);
typedef EFI_STATUS(EFIAPI *EFI_FILE_GET_INFO)(
    struct EFI_FILE_PROTOCOL *, const struct EFI_GUID *, EFI_UINTN *, void *);

struct EFI_FILE_PROTOCOL {
    uint64_t revision;
    EFI_FILE_OPEN open;
    EFI_FILE_CLOSE close;
    EFI_FILE_CLOSE delete_file;
    EFI_FILE_READ read;
    EFI_FILE_READ write;
    void *get_position;
    void *set_position;
    EFI_FILE_GET_INFO get_info;
    void *set_info;
    EFI_FILE_CLOSE flush;
    void *open_ex;
    void *read_ex;
    void *write_ex;
    void *flush_ex;
};

struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef EFI_STATUS(EFIAPI *EFI_OPEN_VOLUME)(
    struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *, struct EFI_FILE_PROTOCOL **);
struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    uint64_t revision;
    EFI_OPEN_VOLUME open_volume;
};

struct EFI_LOAD_FILE2_PROTOCOL;
typedef EFI_STATUS(EFIAPI *EFI_LOAD_FILE2)(
    struct EFI_LOAD_FILE2_PROTOCOL *, struct EFI_DEVICE_PATH_PROTOCOL *,
    EFI_BOOLEAN, EFI_UINTN *, void *);
struct EFI_LOAD_FILE2_PROTOCOL {
    EFI_LOAD_FILE2 load_file;
};

struct EFI_FILE_INFO_PREFIX {
    uint64_t size;
    uint64_t file_size;
    uint64_t physical_size;
};

#define EFI_FILE_MODE_READ UINT64_C(1)

/*
 * These offsets are part of the firmware ABI. Keep the declarations minimal,
 * but fail the build if host packing or an edit moves a service we call.
 */
_Static_assert(sizeof(struct EFI_GUID) == 16, "EFI_GUID layout");
_Static_assert(sizeof(struct EFI_DEVICE_PATH_PROTOCOL) == 4,
               "EFI device-path header layout");
_Static_assert(sizeof(struct EFI_INITRD_DEVICE_PATH) == 24,
               "EFI initrd device-path layout");
_Static_assert(sizeof(struct EFI_TABLE_HEADER) == 24, "EFI table header layout");
_Static_assert(sizeof(struct EFI_MEMORY_DESCRIPTOR) == 40,
               "EFI memory descriptor prefix layout");
_Static_assert(offsetof(struct EFI_BOOT_SERVICES, allocate_pages) == 40,
               "EFI AllocatePages offset");
_Static_assert(offsetof(struct EFI_BOOT_SERVICES, free_pages) == 48,
               "EFI FreePages offset");
_Static_assert(offsetof(struct EFI_BOOT_SERVICES, get_memory_map) == 56,
               "EFI GetMemoryMap offset");
_Static_assert(offsetof(struct EFI_BOOT_SERVICES, exit_boot_services) == 232,
               "EFI ExitBootServices offset");
_Static_assert(offsetof(struct EFI_BOOT_SERVICES, stall) == 248,
               "EFI Stall offset");
_Static_assert(offsetof(struct EFI_BOOT_SERVICES, locate_protocol) == 320,
               "EFI LocateProtocol offset");
_Static_assert(offsetof(struct EFI_BOOT_SERVICES, copy_mem) == 352,
               "EFI CopyMem offset");
_Static_assert(offsetof(struct EFI_BOOT_SERVICES, set_mem) == 360,
               "EFI SetMem offset");
_Static_assert(offsetof(struct EFI_BOOT_SERVICES, handle_protocol) == 152,
               "EFI HandleProtocol offset");
_Static_assert(offsetof(struct EFI_BOOT_SERVICES, locate_device_path) == 184,
               "EFI LocateDevicePath offset");
_Static_assert(offsetof(struct EFI_BOOT_SERVICES, open_protocol) == 280,
               "EFI OpenProtocol offset");
_Static_assert(offsetof(struct EFI_SYSTEM_TABLE, boot_services) == 96,
               "EFI BootServices table offset");
_Static_assert(offsetof(struct EFI_SYSTEM_TABLE, configuration_table) == 112,
               "EFI ConfigurationTable offset");
_Static_assert(offsetof(struct EFI_LOADED_IMAGE_PROTOCOL, device_handle) == 24,
               "EFI LoadedImage DeviceHandle offset");
_Static_assert(offsetof(struct EFI_FILE_PROTOCOL, open) == 8,
               "EFI File Open offset");
_Static_assert(offsetof(struct EFI_FILE_PROTOCOL, read) == 32,
               "EFI File Read offset");
_Static_assert(offsetof(struct EFI_FILE_PROTOCOL, get_info) == 64,
               "EFI File GetInfo offset");
_Static_assert(offsetof(struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL, open_volume) == 8,
               "EFI SimpleFileSystem OpenVolume offset");
_Static_assert(sizeof(struct EFI_FILE_INFO_PREFIX) == 24,
               "EFI FileInfo prefix layout");

#endif /* AROSBOOT_UEFI_H */
