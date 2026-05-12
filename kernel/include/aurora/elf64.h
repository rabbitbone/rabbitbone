#ifndef AURORA_ELF64_H
#define AURORA_ELF64_H
#include <aurora/types.h>
#include <aurora/vfs.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define ELF64_EI_NIDENT 16u
#define ELF64_PT_LOAD 1u
#define ELF64_ET_EXEC 2u
#define ELF64_ET_DYN 3u
#define ELF64_EM_X86_64 62u
#define ELF64_PF_X 1u
#define ELF64_PF_W 2u
#define ELF64_PF_R 4u
#define ELF64_AURORA_USER_IMAGE_BASE 0x0000010000000000ull
#define ELF64_AURORA_USER_SPACE_LIMIT 0x0000800000000000ull

typedef enum elf_status {
    ELF_OK = 0,
    ELF_ERR_IO = -1,
    ELF_ERR_FORMAT = -2,
    ELF_ERR_UNSUPPORTED = -3,
    ELF_ERR_NOMEM = -4,
    ELF_ERR_RANGE = -5,
} elf_status_t;

typedef struct AURORA_PACKED elf64_ehdr {
    u8 e_ident[ELF64_EI_NIDENT];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} elf64_ehdr_t;

typedef struct AURORA_PACKED elf64_phdr {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
} elf64_phdr_t;

typedef struct elf_loaded_segment {
    u64 vaddr;
    u64 memsz;
    u64 filesz;
    u32 flags;
    void *memory;
} elf_loaded_segment_t;

typedef struct elf_loaded_image {
    u64 entry;
    u16 segment_count;
    elf_loaded_segment_t *segments;
} elf_loaded_image_t;

elf_status_t elf64_validate_header(const elf64_ehdr_t *hdr, u64 file_size);
elf_status_t elf64_validate_load_segment(const elf64_phdr_t *ph, u64 file_size, u64 user_min, u64 user_limit);
bool elf64_ranges_overlap(u64 a_start, u64 a_end, u64 b_start, u64 b_end);
elf_status_t elf64_load_from_vfs(const char *path, elf_loaded_image_t *out);
void elf64_free_image(elf_loaded_image_t *img);
const char *elf_status_name(elf_status_t st);

#if defined(__cplusplus)
}
#endif
#endif
