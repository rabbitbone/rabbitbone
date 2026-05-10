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
    u64 ph_end = le64(h->e_phoff) + (u64)le16(h->e_phnum) * le16(h->e_phentsize);
    if (ph_end < le64(h->e_phoff) || ph_end > file_size) return ELF_ERR_RANGE;
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
    u16 seg_index = 0;
    for (u16 i = 0; i < phnum; ++i) {
        if (le32(ph[i].p_type) != ELF64_PT_LOAD) continue;
        u64 off = le64(ph[i].p_offset);
        u64 filesz = le64(ph[i].p_filesz);
        u64 memsz = le64(ph[i].p_memsz);
        if (filesz > memsz || off + filesz < off || off + filesz > st.size) { es = ELF_ERR_RANGE; goto fail; }
        if (memsz == 0 || memsz > 64ull * 1024ull * 1024ull) { es = ELF_ERR_UNSUPPORTED; goto fail; }
        void *mem = kmalloc((usize)memsz);
        if (!mem) { es = ELF_ERR_NOMEM; goto fail; }
        memset(mem, 0, (usize)memsz);
        if (filesz) {
            es = read_exact(path, off, mem, (usize)filesz);
            if (es != ELF_OK) { kfree(mem); goto fail; }
        }
        out->segments[seg_index].vaddr = le64(ph[i].p_vaddr);
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
