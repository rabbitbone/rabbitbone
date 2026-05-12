#include <aurora/elf64.h>
#include <aurora/kmem.h>
#include <aurora/libc.h>
#include <aurora/log.h>

static u16 le16(u16 v) { return v; }
static u32 le32(u32 v) { return v; }
static u64 le64(u64 v) { return v; }

const char *elf_status_name(elf_status_t st) {
    switch (st) {
        case ELF_OK: return "ok";
        case ELF_ERR_IO: return "io";
        case ELF_ERR_FORMAT: return "format";
        case ELF_ERR_UNSUPPORTED: return "unsupported";
        case ELF_ERR_NOMEM: return "no-memory";
        case ELF_ERR_RANGE: return "range";
        default: return "unknown";
    }
}

static bool elf64_is_power_of_two(u64 v) {
    return v && ((v & (v - 1u)) == 0);
}

bool elf64_ranges_overlap(u64 a_start, u64 a_end, u64 b_start, u64 b_end) {
    return a_start < b_end && b_start < a_end;
}

elf_status_t elf64_validate_load_segment(const elf64_phdr_t *ph, u64 file_size, u64 user_min, u64 user_limit) {
    if (!ph || le32(ph->p_type) != ELF64_PT_LOAD) return ELF_ERR_FORMAT;
    u64 off = le64(ph->p_offset);
    u64 vaddr = le64(ph->p_vaddr);
    u64 filesz = le64(ph->p_filesz);
    u64 memsz = le64(ph->p_memsz);
    u64 align = le64(ph->p_align);
    u32 pflags = le32(ph->p_flags);
    if (filesz > memsz) return ELF_ERR_FORMAT;
    if (pflags & ~(ELF64_PF_X | ELF64_PF_W | ELF64_PF_R)) return ELF_ERR_FORMAT;
    if ((pflags & ELF64_PF_X) && (pflags & ELF64_PF_W)) return ELF_ERR_UNSUPPORTED;
    if (align > 1u && !elf64_is_power_of_two(align)) return ELF_ERR_FORMAT;
    if (align > 1u && ((vaddr - off) & (align - 1u)) != 0) return ELF_ERR_FORMAT;
    u64 file_end = 0;
    if (__builtin_add_overflow(off, filesz, &file_end) || file_end > file_size) return ELF_ERR_RANGE;
    u64 mem_end = 0;
    if (__builtin_add_overflow(vaddr, memsz, &mem_end)) return ELF_ERR_RANGE;
    if (memsz && (vaddr < user_min || mem_end > user_limit)) return ELF_ERR_RANGE;
    return ELF_OK;
}

elf_status_t elf64_validate_header(const elf64_ehdr_t *h, u64 file_size) {
    if (!h || file_size < sizeof(*h)) return ELF_ERR_FORMAT;
    if (h->e_ident[0] != 0x7f || h->e_ident[1] != 'E' || h->e_ident[2] != 'L' || h->e_ident[3] != 'F') return ELF_ERR_FORMAT;
    if (h->e_ident[4] != 2u || h->e_ident[5] != 1u || h->e_ident[6] != 1u) return ELF_ERR_UNSUPPORTED;
    if (le16(h->e_type) != ELF64_ET_EXEC && le16(h->e_type) != ELF64_ET_DYN) return ELF_ERR_UNSUPPORTED;
    if (le16(h->e_machine) != ELF64_EM_X86_64) return ELF_ERR_UNSUPPORTED;
    if (le32(h->e_version) != 1u) return ELF_ERR_FORMAT;
    if (le16(h->e_ehsize) != sizeof(elf64_ehdr_t)) return ELF_ERR_FORMAT;
    if (le16(h->e_phentsize) != sizeof(elf64_phdr_t)) return ELF_ERR_FORMAT;
    if (le16(h->e_phnum) == 0 || le16(h->e_phnum) > 64u) return ELF_ERR_FORMAT;
    u64 ph_size = 0;
    u64 ph_end = 0;
    if (__builtin_mul_overflow((u64)le16(h->e_phnum), (u64)le16(h->e_phentsize), &ph_size) ||
        __builtin_add_overflow(le64(h->e_phoff), ph_size, &ph_end) || ph_end > file_size) return ELF_ERR_RANGE;
    return ELF_OK;
}

static elf_status_t read_exact(const char *path, u64 off, void *buf, usize size) {
    usize got = 0;
    vfs_status_t st = vfs_read(path, off, buf, size, &got);
    if (st != VFS_OK) return ELF_ERR_IO;
    return got == size ? ELF_OK : ELF_ERR_RANGE;
}

void elf64_free_image(elf_loaded_image_t *img) {
    if (!img) return;
    if (img->segments) {
        for (u16 i = 0; i < img->segment_count; ++i) kfree(img->segments[i].memory);
        kfree(img->segments);
    }
    memset(img, 0, sizeof(*img));
}

elf_status_t elf64_load_from_vfs(const char *path, elf_loaded_image_t *out) {
    if (!path || !out) return ELF_ERR_FORMAT;
    memset(out, 0, sizeof(*out));
    vfs_stat_t st;
    if (vfs_stat(path, &st) != VFS_OK || st.type != VFS_NODE_FILE) return ELF_ERR_IO;
    if (st.size > 64ull * 1024ull * 1024ull) return ELF_ERR_UNSUPPORTED;
    elf64_ehdr_t eh;
    elf_status_t es = read_exact(path, 0, &eh, sizeof(eh));
    if (es != ELF_OK) return es;
    es = elf64_validate_header(&eh, st.size);
    if (es != ELF_OK) return es;
    if (le64(eh.e_entry) < ELF64_AURORA_USER_IMAGE_BASE || le64(eh.e_entry) >= ELF64_AURORA_USER_SPACE_LIMIT) return ELF_ERR_RANGE;
    u16 phnum = le16(eh.e_phnum);
    elf64_phdr_t *ph = (elf64_phdr_t *)kcalloc(phnum, sizeof(elf64_phdr_t));
    if (!ph) return ELF_ERR_NOMEM;
    es = read_exact(path, le64(eh.e_phoff), ph, (usize)phnum * sizeof(elf64_phdr_t));
    if (es != ELF_OK) { kfree(ph); return es; }
    u16 load_count = 0;
    for (u16 i = 0; i < phnum; ++i) if (le32(ph[i].p_type) == ELF64_PT_LOAD) ++load_count;
    if (!load_count) { kfree(ph); return ELF_ERR_FORMAT; }
    out->segments = (elf_loaded_segment_t *)kcalloc(load_count, sizeof(elf_loaded_segment_t));
    if (!out->segments) { kfree(ph); return ELF_ERR_NOMEM; }
    out->entry = le64(eh.e_entry);
    out->segment_count = load_count;
    u64 loaded_starts[64];
    u64 loaded_ends[64];
    memset(loaded_starts, 0, sizeof(loaded_starts));
    memset(loaded_ends, 0, sizeof(loaded_ends));
    u16 loaded_count = 0;
    u16 seg_index = 0;
    for (u16 i = 0; i < phnum; ++i) {
        if (le32(ph[i].p_type) != ELF64_PT_LOAD) continue;
        es = elf64_validate_load_segment(&ph[i], st.size, ELF64_AURORA_USER_IMAGE_BASE, ELF64_AURORA_USER_SPACE_LIMIT);
        if (es != ELF_OK) goto fail;
        u64 off = le64(ph[i].p_offset);
        u64 vaddr = le64(ph[i].p_vaddr);
        u64 filesz = le64(ph[i].p_filesz);
        u64 memsz = le64(ph[i].p_memsz);
        u64 mem_end = vaddr + memsz;
        if (memsz == 0 || memsz > 64ull * 1024ull * 1024ull) { es = ELF_ERR_UNSUPPORTED; goto fail; }
        for (u16 j = 0; j < loaded_count; ++j) {
            if (elf64_ranges_overlap(vaddr, mem_end, loaded_starts[j], loaded_ends[j])) { es = ELF_ERR_FORMAT; goto fail; }
        }
        loaded_starts[loaded_count] = vaddr;
        loaded_ends[loaded_count] = mem_end;
        ++loaded_count;
        void *mem = kmalloc((usize)memsz);
        if (!mem) { es = ELF_ERR_NOMEM; goto fail; }
        memset(mem, 0, (usize)memsz);
        if (filesz) {
            es = read_exact(path, off, mem, (usize)filesz);
            if (es != ELF_OK) { kfree(mem); goto fail; }
        }
        out->segments[seg_index].vaddr = vaddr;
        out->segments[seg_index].memsz = memsz;
        out->segments[seg_index].filesz = filesz;
        out->segments[seg_index].flags = le32(ph[i].p_flags);
        out->segments[seg_index].memory = mem;
        ++seg_index;
    }
    kfree(ph);
    KLOG(LOG_INFO, "elf", "loaded %s entry=%p segments=%u", path, (void *)(uptr)out->entry, out->segment_count);
    return ELF_OK;
fail:
    kfree(ph);
    elf64_free_image(out);
    return es;
}
