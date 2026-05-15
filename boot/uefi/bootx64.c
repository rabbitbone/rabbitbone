#include <rabbitbone/bootinfo.h>

#define EFIAPI __attribute__((ms_abi))
#if defined(__x86_64__)
#define RABBITBONE_SYSV_ABI __attribute__((sysv_abi))
#else
#define RABBITBONE_SYSV_ABI
#endif

typedef u64 UINT64;
typedef u32 UINT32;
typedef u16 UINT16;
typedef u8 UINT8;
typedef u64 UINTN;
typedef u64 EFI_STATUS;
typedef u64 EFI_PHYSICAL_ADDRESS;
typedef u64 EFI_VIRTUAL_ADDRESS;
typedef void *EFI_HANDLE;
typedef void *EFI_EVENT;
typedef u16 CHAR16;
typedef u8 BOOLEAN;

typedef struct EFI_GUID {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8 Data4[8];
} EFI_GUID;

#define EFI_SUCCESS 0ull
#define EFI_BUFFER_TOO_SMALL 0x8000000000000005ull
#define EFI_NOT_FOUND 0x800000000000000Eull
#define EFI_INVALID_PARAMETER 0x8000000000000002ull
#define EFI_UNSUPPORTED 0x8000000000000003ull
#define EFI_OUT_OF_RESOURCES 0x8000000000000009ull
#define EFI_ERROR(x) (((x) & 0x8000000000000000ull) != 0)

#define EFI_ALLOCATE_ANY_PAGES 0u
#define EFI_ALLOCATE_MAX_ADDRESS 1u
#define EFI_ALLOCATE_ADDRESS 2u

#define EFI_LOADER_CODE 1u
#define EFI_LOADER_DATA 2u
#define EFI_BOOT_SERVICES_CODE 3u
#define EFI_BOOT_SERVICES_DATA 4u
#define EFI_CONVENTIONAL_MEMORY 7u
#define EFI_ACPI_RECLAIM_MEMORY 9u
#define EFI_MEMORY_MAPPED_IO 11u
#define EFI_MEMORY_MAPPED_IO_PORT_SPACE 12u
#define EFI_PAL_CODE 13u
#define EFI_PERSISTENT_MEMORY 14u

#define EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL 0x00000001u
#define EFI_FILE_MODE_READ 0x0000000000000001ull
#define EFI_FILE_DIRECTORY 0x0000000000000010ull

#define RABBITBONE_UEFI_MAX_MEMORY_MAP 128u
#define RABBITBONE_UEFI_MAX_FILE_BYTES (96ull * 1024ull * 1024ull)
#define RABBITBONE_UEFI_KERNEL_BASE 0x10000ull
#define RABBITBONE_UEFI_KERNEL_LIMIT 0x9f000ull
#define RABBITBONE_UEFI_KERNEL_MAX_BYTES (RABBITBONE_UEFI_KERNEL_LIMIT - RABBITBONE_UEFI_KERNEL_BASE)
#define RABBITBONE_UEFI_LOW_MAX 0x3fffffffull
#define RABBITBONE_UEFI_BOOTINFO_PAGES 4u
#define RABBITBONE_UEFI_MMAP_INITIAL_PAGES 16u
#define RABBITBONE_UEFI_MMAP_MAX_PAGES 128u
#define RABBITBONE_UEFI_FILE_INFO_MAX 4096u
#define RABBITBONE_UEFI_BOOTINFO_OFFSET 0u
#define RABBITBONE_UEFI_MODULES_OFFSET 4096u
#define RABBITBONE_UEFI_CMDLINE_OFFSET 8192u
#define RABBITBONE_UEFI_NAME_OFFSET 8704u
#define RABBITBONE_UEFI_E820_OFFSET 12288u

static const EFI_GUID EFI_LOADED_IMAGE_PROTOCOL_GUID = {0x5B1B31A1u, 0x9562u, 0x11D2u, {0x8Eu,0x3Fu,0x00u,0xA0u,0xC9u,0x69u,0x72u,0x3Bu}};
static const EFI_GUID EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID = {0x964E5B22u, 0x6459u, 0x11D2u, {0x8Eu,0x39u,0x00u,0xA0u,0xC9u,0x69u,0x72u,0x3Bu}};
static const EFI_GUID EFI_FILE_INFO_ID = {0x09576E92u, 0x6D3Fu, 0x11D2u, {0x8Eu,0x39u,0x00u,0xA0u,0xC9u,0x69u,0x72u,0x3Bu}};
static const EFI_GUID EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID = {0x9042A9DEu, 0x23DCu, 0x4A38u, {0x96u,0xFBu,0x7Au,0xDEu,0xD0u,0x80u,0x51u,0x6Au}};

typedef struct EFI_TABLE_HEADER {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef struct EFI_TIME {
    UINT16 Year;
    UINT8 Month;
    UINT8 Day;
    UINT8 Hour;
    UINT8 Minute;
    UINT8 Second;
    UINT8 Pad1;
    UINT32 Nanosecond;
    i16 TimeZone;
    UINT8 Daylight;
    UINT8 Pad2;
} EFI_TIME;

typedef struct EFI_MEMORY_DESCRIPTOR {
    UINT32 Type;
    UINT32 Pad;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_TEXT_RESET)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, BOOLEAN);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, CHAR16 *);
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_TEXT_RESET Reset;
    EFI_TEXT_STRING OutputString;
    void *TestString;
    void *QueryMode;
    void *SetMode;
    void *SetAttribute;
    void *ClearScreen;
    void *SetCursorPosition;
    void *EnableCursor;
    void *Mode;
};

typedef struct EFI_BOOT_SERVICES EFI_BOOT_SERVICES;
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(UINT32, UINT32, UINTN, EFI_PHYSICAL_ADDRESS *);
typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(EFI_PHYSICAL_ADDRESS, UINTN);
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(UINTN *, EFI_MEMORY_DESCRIPTOR *, UINTN *, UINTN *, UINT32 *);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(UINT32, UINTN, void **);
typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(void *);
typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(EFI_HANDLE, EFI_GUID *, void **);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE, UINTN);
typedef EFI_STATUS (EFIAPI *EFI_OPEN_PROTOCOL)(EFI_HANDLE, EFI_GUID *, void **, EFI_HANDLE, EFI_HANDLE, UINT32);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(EFI_GUID *, void *, void **);
typedef EFI_STATUS (EFIAPI *EFI_STALL)(UINTN);

struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL;
    void *RestoreTPL;
    EFI_ALLOCATE_PAGES AllocatePages;
    EFI_FREE_PAGES FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_ALLOCATE_POOL AllocatePool;
    EFI_FREE_POOL FreePool;
    void *CreateEvent;
    void *SetTimer;
    void *WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;
    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL HandleProtocol;
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;
    void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;
    void *GetNextMonotonicCount;
    EFI_STALL Stall;
    void *SetWatchdogTimer;
    void *ConnectController;
    void *DisconnectController;
    EFI_OPEN_PROTOCOL OpenProtocol;
    void *CloseProtocol;
    void *OpenProtocolInformation;
    void *ProtocolsPerHandle;
    void *LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL LocateProtocol;
};

typedef struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct EFI_LOADED_IMAGE_PROTOCOL {
    UINT32 Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    void *FilePath;
    void *Reserved;
    UINT32 LoadOptionsSize;
    void *LoadOptions;
    void *ImageBase;
    UINT64 ImageSize;
    UINT32 ImageCodeType;
    UINT32 ImageDataType;
    EFI_STATUS (EFIAPI *Unload)(EFI_HANDLE);
} EFI_LOADED_IMAGE_PROTOCOL;

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **, CHAR16 *, UINT64, UINT64);
typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL *);
typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(EFI_FILE_PROTOCOL *, UINTN *, void *);
typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_INFO)(EFI_FILE_PROTOCOL *, EFI_GUID *, UINTN *, void *);
struct EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_FILE_OPEN Open;
    EFI_FILE_CLOSE Close;
    void *Delete;
    EFI_FILE_READ Read;
    void *Write;
    void *GetPosition;
    void *SetPosition;
    EFI_FILE_GET_INFO GetInfo;
};

typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (EFIAPI *OpenVolume)(struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *, EFI_FILE_PROTOCOL **);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef enum EFI_GRAPHICS_PIXEL_FORMAT {
    PixelRedGreenBlueReserved8BitPerColor = 0,
    PixelBlueGreenRedReserved8BitPerColor = 1,
    PixelBitMask = 2,
    PixelBltOnly = 3,
    PixelFormatMax = 4
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct EFI_PIXEL_BITMASK {
    UINT32 RedMask;
    UINT32 GreenMask;
    UINT32 BlueMask;
    UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct EFI_GRAPHICS_OUTPUT_MODE_INFORMATION {
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE {
    UINT32 MaxMode;
    UINT32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    void *QueryMode;
    void *SetMode;
    void *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef struct EFI_FILE_INFO_SHORT {
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    EFI_TIME CreateTime;
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    UINT64 Attribute;
    CHAR16 FileName[1];
} EFI_FILE_INFO_SHORT;

static EFI_SYSTEM_TABLE *g_st;
static EFI_BOOT_SERVICES *g_bs;

RABBITBONE_STATIC_ASSERT(uefi_bootinfo_fits_slot, sizeof(rabbitbone_bootinfo_t) <= 512u);
RABBITBONE_STATIC_ASSERT(uefi_module_fits_slot, sizeof(rabbitbone_boot_module_t) <= 512u);
RABBITBONE_STATIC_ASSERT(uefi_e820_fits_page, sizeof(rabbitbone_e820_entry_t) * RABBITBONE_UEFI_MAX_MEMORY_MAP <= 4096u);

static void *memset_local(void *dst, int v, UINTN n) {
    UINT8 *p = (UINT8 *)dst;
    while (n--) *p++ = (UINT8)v;
    return dst;
}

static void puts16(const CHAR16 *s) {
    if (g_st && g_st->ConOut && s) (void)g_st->ConOut->OutputString(g_st->ConOut, (CHAR16 *)s);
}

static void put_status(const CHAR16 *prefix, EFI_STATUS st) {
    static CHAR16 buf[64];
    static const CHAR16 hex[] = { '0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f' };
    UINTN n = 0;
    while (prefix && prefix[n] && n + 1u < 32u) { buf[n] = prefix[n]; ++n; }
    buf[n++] = '0'; buf[n++] = 'x';
    for (int i = 60; i >= 0; i -= 4) buf[n++] = hex[(st >> (UINTN)i) & 0xfu];
    buf[n++] = '\r'; buf[n++] = '\n'; buf[n] = 0;
    puts16(buf);
}


static bool cpu_has_nxe(void) {
    UINT32 eax = 0x80000000u;
    UINT32 ebx = 0;
    UINT32 ecx = 0;
    UINT32 edx = 0;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (eax < 0x80000001u) return false;
    eax = 0x80000001u;
    ebx = ecx = edx = 0;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (edx & (1u << 20)) != 0;
}

static EFI_STATUS enable_nxe_checked(void) {
    if (!cpu_has_nxe()) return EFI_UNSUPPORTED;
    UINT32 lo = 0;
    UINT32 hi = 0;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xc0000080u));
    lo |= (1u << 11);
    __asm__ volatile("wrmsr" :: "c"(0xc0000080u), "a"(lo), "d"(hi) : "memory");
    return EFI_SUCCESS;
}

static UINTN pages_for(UINT64 bytes) {
    if (bytes > 0xfffffffffffff000ull) return 0;
    return (UINTN)((bytes + 4095ull) >> 12);
}

static EFI_STATUS file_size(EFI_FILE_PROTOCOL *file, UINT64 *out) {
    if (!file || !out || !g_bs) return EFI_INVALID_PARAMETER;
    *out = 0;
    UINTN info_size = 0;
    EFI_STATUS st = file->GetInfo(file, (EFI_GUID *)&EFI_FILE_INFO_ID, &info_size, 0);
    if (st != EFI_BUFFER_TOO_SMALL || info_size < sizeof(EFI_FILE_INFO_SHORT) || info_size > RABBITBONE_UEFI_FILE_INFO_MAX) return EFI_NOT_FOUND;
    void *info_buf = 0;
    st = g_bs->AllocatePool(EFI_LOADER_DATA, info_size, &info_buf);
    if (EFI_ERROR(st)) return st;
    st = file->GetInfo(file, (EFI_GUID *)&EFI_FILE_INFO_ID, &info_size, info_buf);
    if (!EFI_ERROR(st)) {
        const EFI_FILE_INFO_SHORT *info = (const EFI_FILE_INFO_SHORT *)info_buf;
        if ((info->Attribute & EFI_FILE_DIRECTORY) != 0) st = EFI_INVALID_PARAMETER;
        else *out = info->FileSize;
    }
    (void)g_bs->FreePool(info_buf);
    return st;
}

static EFI_STATUS read_file_alloc(EFI_FILE_PROTOCOL *root, const CHAR16 *path, UINT32 page_type, UINT32 memory_type,
                                  EFI_PHYSICAL_ADDRESS address_hint, UINT64 max_file_bytes, void **base_out, UINT64 *size_out) {
    if (!root || !path || !base_out || !size_out || max_file_bytes == 0) return EFI_INVALID_PARAMETER;
    *base_out = 0;
    *size_out = 0;
    EFI_FILE_PROTOCOL *file = 0;
    EFI_STATUS st = root->Open(root, &file, (CHAR16 *)path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(st)) return st;
    UINT64 size = 0;
    st = file_size(file, &size);
    if (EFI_ERROR(st) || size == 0 || size > max_file_bytes) {
        (void)file->Close(file);
        return EFI_NOT_FOUND;
    }
    UINTN page_count = pages_for(size);
    if (page_count == 0) {
        (void)file->Close(file);
        return EFI_NOT_FOUND;
    }
    EFI_PHYSICAL_ADDRESS addr = address_hint;
    st = g_bs->AllocatePages(page_type, memory_type, page_count, &addr);
    if (EFI_ERROR(st)) {
        (void)file->Close(file);
        return st;
    }
    memset_local((void *)(uptr)addr, 0, page_count << 12);
    UINTN read_size = (UINTN)size;
    st = file->Read(file, &read_size, (void *)(uptr)addr);
    (void)file->Close(file);
    if (EFI_ERROR(st) || read_size != (UINTN)size) {
        (void)g_bs->FreePages(addr, page_count);
        return EFI_NOT_FOUND;
    }
    *base_out = (void *)(uptr)addr;
    *size_out = size;
    return EFI_SUCCESS;
}

static UINT32 e820_type_from_uefi(UINT32 type) {
    switch (type) {
        case EFI_CONVENTIONAL_MEMORY:
        case EFI_BOOT_SERVICES_CODE:
        case EFI_BOOT_SERVICES_DATA:
            return RABBITBONE_E820_USABLE;
        case EFI_ACPI_RECLAIM_MEMORY:
            return 3u;
        case EFI_MEMORY_MAPPED_IO:
        case EFI_MEMORY_MAPPED_IO_PORT_SPACE:
        case EFI_PAL_CODE:
        case EFI_PERSISTENT_MEMORY:
        default:
            return 2u;
    }
}

static UINT32 framebuffer_format_from_gop(const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info) {
    if (!info) return RABBITBONE_BOOT_FB_FORMAT_NONE;
    if (info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor) return RABBITBONE_BOOT_FB_FORMAT_RGBX;
    if (info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) return RABBITBONE_BOOT_FB_FORMAT_BGRX;
    if (info->PixelFormat == PixelBitMask) {
        if (info->PixelInformation.RedMask == 0x000000ffu &&
            info->PixelInformation.GreenMask == 0x0000ff00u &&
            info->PixelInformation.BlueMask == 0x00ff0000u) return RABBITBONE_BOOT_FB_FORMAT_RGBX;
        if (info->PixelInformation.RedMask == 0x00ff0000u &&
            info->PixelInformation.GreenMask == 0x0000ff00u &&
            info->PixelInformation.BlueMask == 0x000000ffu) return RABBITBONE_BOOT_FB_FORMAT_BGRX;
    }
    return RABBITBONE_BOOT_FB_FORMAT_NONE;
}

static void fill_framebuffer_bootinfo(rabbitbone_bootinfo_t *bootinfo) {
    if (!bootinfo || !g_bs || !g_bs->LocateProtocol) return;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_STATUS st = g_bs->LocateProtocol((EFI_GUID *)&EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, 0, (void **)&gop);
    if (EFI_ERROR(st) || !gop || !gop->Mode || !gop->Mode->Info || gop->Mode->FrameBufferBase == 0) return;
    const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = gop->Mode->Info;
    UINT32 format = framebuffer_format_from_gop(info);
    if (format == RABBITBONE_BOOT_FB_FORMAT_NONE) return;
    if (info->HorizontalResolution == 0 || info->VerticalResolution == 0 || info->PixelsPerScanLine < info->HorizontalResolution) return;
    bootinfo->reserved1[0] = (u64)gop->Mode->FrameBufferBase;
    bootinfo->reserved1[1] = ((u64)info->HorizontalResolution << 32) | (u64)info->VerticalResolution;
    bootinfo->reserved1[2] = ((u64)info->PixelsPerScanLine << 32) | (u64)format;
}

static bool range_end(UINT64 base, UINT64 length, UINT64 *end_out) {
    if (!end_out || length == 0) return false;
    return !__builtin_add_overflow(base, length, end_out) && *end_out >= base;
}

static bool e820_append_or_merge(rabbitbone_e820_entry_t *e820, UINTN *count, UINT64 base, UINT64 length, UINT32 type) {
    if (!e820 || !count || length == 0) return true;
    UINT64 end = 0;
    if (!range_end(base, length, &end)) return true;
    if (*count != 0) {
        rabbitbone_e820_entry_t *prev = &e820[*count - 1u];
        UINT64 prev_end = 0;
        if (prev->type == type && range_end(prev->base, prev->length, &prev_end) && prev_end == base) {
            prev->length += length;
            return true;
        }
    }
    if (*count >= RABBITBONE_UEFI_MAX_MEMORY_MAP) return false;
    e820[*count].base = base;
    e820[*count].length = length;
    e820[*count].type = type;
    e820[*count].acpi = 0;
    ++*count;
    return true;
}

static void e820_sort_and_coalesce(rabbitbone_e820_entry_t *e820, UINTN *count) {
    if (!e820 || !count || *count < 2u) return;
    for (UINTN i = 1u; i < *count; ++i) {
        rabbitbone_e820_entry_t key = e820[i];
        UINTN j = i;
        while (j > 0u && e820[j - 1u].base > key.base) {
            e820[j] = e820[j - 1u];
            --j;
        }
        e820[j] = key;
    }
    UINTN out = 0;
    for (UINTN i = 0; i < *count; ++i) {
        UINT64 cur_end = 0;
        if (!range_end(e820[i].base, e820[i].length, &cur_end)) continue;
        if (out != 0u) {
            rabbitbone_e820_entry_t *prev = &e820[out - 1u];
            UINT64 prev_end = 0;
            if (prev->type == e820[i].type && range_end(prev->base, prev->length, &prev_end) && e820[i].base <= prev_end) {
                if (cur_end > prev_end) prev->length = cur_end - prev->base;
                continue;
            }
        }
        e820[out++] = e820[i];
    }
    *count = out;
}

static bool fill_memory_map_bootinfo(rabbitbone_bootinfo_t *bootinfo, rabbitbone_e820_entry_t *e820,
                                     UINT8 *memory_map_storage, UINTN memory_map_size,
                                     UINTN descriptor_size) {
    if (!bootinfo || !e820 || !memory_map_storage || descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR)) {
        if (bootinfo) bootinfo->e820_count = 0;
        return false;
    }
    UINTN descriptor_count = memory_map_size / descriptor_size;
    UINTN out_count = 0;
    for (UINTN i = 0; i < descriptor_count; ++i) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)(memory_map_storage + i * descriptor_size);
        UINT64 length = 0;
        if (__builtin_mul_overflow(d->NumberOfPages, 4096ull, &length) || length == 0) continue;
        UINT32 type = e820_type_from_uefi(d->Type);
        if (!e820_append_or_merge(e820, &out_count, d->PhysicalStart, length, type)) {
            bootinfo->e820_count = 0;
            return false;
        }
    }
    e820_sort_and_coalesce(e820, &out_count);
    if (out_count > RABBITBONE_UEFI_MAX_MEMORY_MAP) {
        bootinfo->e820_count = 0;
        return false;
    }
    bootinfo->e820_count = (u16)out_count;
    return true;
}

static EFI_STATUS resize_memory_map_buffer(UINT8 **storage, UINTN *capacity, UINTN requested_size) {
    if (!storage || !capacity) return EFI_INVALID_PARAMETER;
    UINT64 desired = requested_size ? requested_size : (UINT64)RABBITBONE_UEFI_MMAP_INITIAL_PAGES * 4096ull;
    if (desired < (UINT64)RABBITBONE_UEFI_MMAP_INITIAL_PAGES * 4096ull) desired = (UINT64)RABBITBONE_UEFI_MMAP_INITIAL_PAGES * 4096ull;
    if (desired > 0xffffffffffffffffull - 8ull * 4096ull) return EFI_OUT_OF_RESOURCES;
    desired += 8ull * 4096ull;
    UINTN pages = pages_for(desired);
    if (pages == 0 || pages > RABBITBONE_UEFI_MMAP_MAX_PAGES) return EFI_OUT_OF_RESOURCES;
    EFI_PHYSICAL_ADDRESS addr = RABBITBONE_UEFI_LOW_MAX;
    EFI_STATUS st = g_bs->AllocatePages(EFI_ALLOCATE_MAX_ADDRESS, EFI_LOADER_DATA, pages, &addr);
    if (EFI_ERROR(st)) return st;
    memset_local((void *)(uptr)addr, 0, pages << 12);
    if (*storage && *capacity) (void)g_bs->FreePages((EFI_PHYSICAL_ADDRESS)(uptr)*storage, *capacity >> 12);
    *storage = (UINT8 *)(uptr)addr;
    *capacity = pages << 12;
    return EFI_SUCCESS;
}

static EFI_STATUS get_memory_map_dynamic(UINT8 **storage, UINTN *capacity, UINTN *memory_map_size,
                                         UINTN *map_key, UINTN *descriptor_size, UINT32 *descriptor_version) {
    if (!storage || !capacity || !memory_map_size || !map_key || !descriptor_size || !descriptor_version) return EFI_INVALID_PARAMETER;
    if (!*storage || *capacity == 0) {
        EFI_STATUS st = resize_memory_map_buffer(storage, capacity, 0);
        if (EFI_ERROR(st)) return st;
    }
    for (UINTN attempt = 0; attempt < 8u; ++attempt) {
        *memory_map_size = *capacity;
        EFI_STATUS st = g_bs->GetMemoryMap(memory_map_size, (EFI_MEMORY_DESCRIPTOR *)*storage, map_key, descriptor_size, descriptor_version);
        if (st == EFI_BUFFER_TOO_SMALL) {
            st = resize_memory_map_buffer(storage, capacity, *memory_map_size);
            if (EFI_ERROR(st)) return st;
            continue;
        }
        if (!EFI_ERROR(st) && *descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR)) return EFI_INVALID_PARAMETER;
        return st;
    }
    return EFI_OUT_OF_RESOURCES;
}

static const CHAR16 kernel_path[] = {'\\','R','A','B','B','I','T','B','O','N','E','\\','K','E','R','N','E','L','.','B','I','N',0};
static const CHAR16 root_path[] = {'\\','R','A','B','B','I','T','B','O','N','E','\\','R','O','O','T','.','I','M','G',0};
static const char module_name[] = "rabbitbone-live-root.img";
static const char cmdline[] = "root=/disk0 init=/disk0/sbin/init userland=live-ramdisk boot=uefi liveiso=1";

RABBITBONE_STATIC_ASSERT(uefi_cmdline_fits_slot, sizeof(cmdline) <= 512u);
RABBITBONE_STATIC_ASSERT(uefi_module_name_fits_slot, sizeof(module_name) <= 512u);

typedef void (RABBITBONE_SYSV_ABI *kernel_entry_t)(const rabbitbone_bootinfo_t *);

EFI_STATUS EFIAPI EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    if (!SystemTable || !SystemTable->BootServices) return EFI_NOT_FOUND;
    g_st = SystemTable;
    g_bs = SystemTable->BootServices;
    puts16((CHAR16 *)L"Rabbitbone\r\n");

    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    EFI_STATUS st = g_bs->HandleProtocol(ImageHandle, (EFI_GUID *)&EFI_LOADED_IMAGE_PROTOCOL_GUID, (void **)&loaded);
    if (EFI_ERROR(st)) { put_status((CHAR16 *)L"LoadedImage failed: ", st); return st; }

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    st = g_bs->HandleProtocol(loaded->DeviceHandle, (EFI_GUID *)&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, (void **)&fs);
    if (EFI_ERROR(st)) { put_status((CHAR16 *)L"SimpleFS failed: ", st); return st; }

    EFI_FILE_PROTOCOL *root = 0;
    st = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(st)) { put_status((CHAR16 *)L"OpenVolume failed: ", st); return st; }

    if (!cpu_has_nxe()) {
        (void)root->Close(root);
        put_status((CHAR16 *)L"CPU NXE unsupported: ", EFI_UNSUPPORTED);
        return EFI_UNSUPPORTED;
    }

    void *kernel = 0;
    UINT64 kernel_size = 0;
    st = read_file_alloc(root, kernel_path, EFI_ALLOCATE_ADDRESS, EFI_LOADER_CODE, RABBITBONE_UEFI_KERNEL_BASE, RABBITBONE_UEFI_KERNEL_MAX_BYTES, &kernel, &kernel_size);
    if (EFI_ERROR(st)) { (void)root->Close(root); put_status((CHAR16 *)L"kernel load failed: ", st); return st; }
    if ((uptr)kernel != (uptr)RABBITBONE_UEFI_KERNEL_BASE) { (void)root->Close(root); put_status((CHAR16 *)L"kernel base mismatch: ", (EFI_STATUS)(uptr)kernel); return EFI_NOT_FOUND; }

    void *root_img = 0;
    UINT64 root_size = 0;
    st = read_file_alloc(root, root_path, EFI_ALLOCATE_MAX_ADDRESS, EFI_LOADER_DATA, RABBITBONE_UEFI_LOW_MAX, RABBITBONE_UEFI_MAX_FILE_BYTES, &root_img, &root_size);
    if (EFI_ERROR(st)) { (void)root->Close(root); put_status((CHAR16 *)L"root image load failed: ", st); return st; }
    (void)root->Close(root);

    EFI_PHYSICAL_ADDRESS low = RABBITBONE_UEFI_LOW_MAX;
    st = g_bs->AllocatePages(EFI_ALLOCATE_MAX_ADDRESS, EFI_LOADER_DATA, RABBITBONE_UEFI_BOOTINFO_PAGES, &low);
    if (EFI_ERROR(st)) { put_status((CHAR16 *)L"bootinfo pages failed: ", st); return st; }
    memset_local((void *)(uptr)low, 0, RABBITBONE_UEFI_BOOTINFO_PAGES << 12);
    rabbitbone_bootinfo_t *bootinfo = (rabbitbone_bootinfo_t *)(uptr)(low + RABBITBONE_UEFI_BOOTINFO_OFFSET);
    rabbitbone_boot_module_t *modules = (rabbitbone_boot_module_t *)(uptr)(low + RABBITBONE_UEFI_MODULES_OFFSET);
    char *cmd = (char *)(uptr)(low + RABBITBONE_UEFI_CMDLINE_OFFSET);
    char *name = (char *)(uptr)(low + RABBITBONE_UEFI_NAME_OFFSET);
    rabbitbone_e820_entry_t *e820 = (rabbitbone_e820_entry_t *)(uptr)(low + RABBITBONE_UEFI_E820_OFFSET);

    for (UINTN i = 0; i < sizeof(cmdline); ++i) cmd[i] = cmdline[i];
    for (UINTN i = 0; i < sizeof(module_name); ++i) name[i] = module_name[i];

    UINT8 *memory_map_storage = 0;
    UINTN memory_map_capacity = 0;
    UINTN memory_map_size = 0;
    UINTN map_key = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;
    st = get_memory_map_dynamic(&memory_map_storage, &memory_map_capacity, &memory_map_size,
                                &map_key, &descriptor_size, &descriptor_version);
    if (EFI_ERROR(st)) { put_status((CHAR16 *)L"GetMemoryMap failed: ", st); return st; }

    modules[0].base = (u64)(uptr)root_img;
    modules[0].size = root_size;
    modules[0].name_addr = (u64)(uptr)name;
    modules[0].reserved = 0;

    bootinfo->magic = RABBITBONE_BOOT_MAGIC;
    bootinfo->version = RABBITBONE_BOOT_VERSION;
    bootinfo->e820_count = 0;
    bootinfo->module_count = 1;
    bootinfo->flags = RABBITBONE_BOOT_FLAG_UEFI | RABBITBONE_BOOT_FLAG_LIVE_RAMDISK;
    bootinfo->boot_drive = 0;
    bootinfo->e820_addr = (u64)(uptr)e820;
    bootinfo->kernel_lba = 0;
    bootinfo->kernel_sectors = (kernel_size + 511ull) >> 9;
    bootinfo->modules_addr = (u64)(uptr)modules;
    bootinfo->root_lba = 0;
    bootinfo->root_sectors = (root_size + 511ull) >> 9;
    bootinfo->cmdline_addr = (u64)(uptr)cmd;
    bootinfo->cmdline_size = (u32)(sizeof(cmdline));
    fill_framebuffer_bootinfo(bootinfo);

    st = get_memory_map_dynamic(&memory_map_storage, &memory_map_capacity, &memory_map_size,
                                &map_key, &descriptor_size, &descriptor_version);
    if (EFI_ERROR(st)) { put_status((CHAR16 *)L"final GetMemoryMap failed: ", st); return st; }
    if (!fill_memory_map_bootinfo(bootinfo, e820, memory_map_storage, memory_map_size, descriptor_size)) {
        put_status((CHAR16 *)L"memory map too large: ", EFI_OUT_OF_RESOURCES);
        return EFI_OUT_OF_RESOURCES;
    }
    st = g_bs->ExitBootServices(ImageHandle, map_key);
    if (EFI_ERROR(st)) {
        st = get_memory_map_dynamic(&memory_map_storage, &memory_map_capacity, &memory_map_size,
                                    &map_key, &descriptor_size, &descriptor_version);
        if (!EFI_ERROR(st)) {
            if (!fill_memory_map_bootinfo(bootinfo, e820, memory_map_storage, memory_map_size, descriptor_size)) {
                put_status((CHAR16 *)L"memory map too large: ", EFI_OUT_OF_RESOURCES);
                return EFI_OUT_OF_RESOURCES;
            }
            st = g_bs->ExitBootServices(ImageHandle, map_key);
        }
        if (EFI_ERROR(st)) return st;
    }

    __asm__ volatile("cli" ::: "memory");
    (void)enable_nxe_checked();
    ((kernel_entry_t)(uptr)RABBITBONE_UEFI_KERNEL_BASE)(bootinfo);
    for (;;) __asm__ volatile("hlt");
    return EFI_SUCCESS;
}
