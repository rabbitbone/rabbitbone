#include <rabbitbone/path.h>
#include <rabbitbone/vfs.h>
#include <rabbitbone/libc.h>

bool path_is_absolute(const char *path) { return path && path[0] == '/'; }

static bool path_fail(char *out, usize out_size) {
    if (out && out_size) out[0] = 0;
    return false;
}

static bool path_snapshot_input(const char *in, char tmp[VFS_PATH_MAX], usize *len_out) {
    if (!in || !tmp || !len_out) return false;
    usize in_len = strnlen(in, VFS_PATH_MAX);
    if (in_len == 0 || in_len >= VFS_PATH_MAX || in[0] != '/') return false;
    memcpy(tmp, in, in_len);
    tmp[in_len] = 0;
    *len_out = in_len;
    return true;
}

static bool path_pop_component(char *out, usize *pos) {
    if (!out || !pos || *pos <= 1u) return false;
    while (*pos > 1u && out[*pos - 1u] != '/') --*pos;
    if (*pos > 1u && out[*pos - 1u] == '/') --*pos;
    out[*pos] = 0;
    return true;
}

static bool path_component_byte_allowed(char c) {
    unsigned char uc = (unsigned char)c;
    return uc >= 0x20u && uc != 0x7fu && c != '\\';
}

static bool path_append_component(char *out, usize out_size, usize *pos, const char *comp, usize len) {
    if (!out || !pos || !comp) return false;
    if (len == 0) return true;
    if (len >= VFS_NAME_MAX) return false;
    for (usize i = 0; i < len; ++i) {
        if (!path_component_byte_allowed(comp[i])) return false;
    }
    usize slash = *pos > 1u ? 1u : 0u;
    usize need = slash + len + 1u;
    if (need > out_size || *pos > out_size - need) return false;
    if (slash) out[(*pos)++] = '/';
    memcpy(out + *pos, comp, len);
    *pos += len;
    out[*pos] = 0;
    return true;
}

bool path_normalize(const char *in, char *out, usize out_size) {
    if (out && out_size) out[0] = 0;
    if (!in || !out || out_size < 2u) return false;

    char tmp[VFS_PATH_MAX];
    usize in_len = 0;
    if (!path_snapshot_input(in, tmp, &in_len)) return false;

    usize pos = 0;
    out[pos++] = '/';
    out[pos] = 0;

    usize i = 0;
    while (i < in_len) {
        while (i < in_len && tmp[i] == '/') ++i;
        usize start = i;
        while (i < in_len && tmp[i] != '/') ++i;
        usize len = i - start;
        if (len == 0 || (len == 1u && tmp[start] == '.')) continue;
        if (len == 2u && tmp[start] == '.' && tmp[start + 1u] == '.') {
            if (!path_pop_component(out, &pos)) return path_fail(out, out_size);
            continue;
        }
        if (!path_append_component(out, out_size, &pos, tmp + start, len)) return path_fail(out, out_size);
    }
    return true;
}

const char *path_basename(const char *path) {
    if (!path || !*path) return "";
    const char *last = path;
    for (const char *p = path; *p; ++p) if (*p == '/' && p[1]) last = p + 1;
    return last;
}

bool path_join(const char *base, const char *leaf, char *out, usize out_size) {
    if (!base || !leaf || !out) return false;
    if (out_size) out[0] = 0;
    char tmp[VFS_PATH_MAX * 2u];
    usize bl = strnlen(base, VFS_PATH_MAX);
    usize ll = strnlen(leaf, VFS_PATH_MAX);
    if (bl == 0 || bl >= VFS_PATH_MAX || ll >= VFS_PATH_MAX) return false;
    usize need = 0;
    if (__builtin_add_overflow(bl, ll, &need) || __builtin_add_overflow(need, 2u, &need)) return false;
    if (need > sizeof(tmp)) return false;
    memcpy(tmp, base, bl);
    usize pos = bl;
    if (pos == 0 || tmp[pos - 1u] != '/') tmp[pos++] = '/';
    memcpy(tmp + pos, leaf, ll);
    pos += ll;
    tmp[pos] = 0;
    return path_normalize(tmp, out, out_size);
}

bool path_next_component(const char **cursor, char *component, usize component_size) {
    if (!cursor || !*cursor || !component || component_size == 0) return false;
    component[0] = 0;
    const char *p = *cursor;
    while (*p == '/') ++p;
    if (!*p) { *cursor = p; return false; }
    const char *start = p;
    while (*p && *p != '/') ++p;
    usize len = (usize)(p - start);
    if (len >= component_size) { component[0] = 0; *cursor = p; return false; }
    for (usize i = 0; i < len; ++i) {
        if (!path_component_byte_allowed(start[i])) { component[0] = 0; *cursor = p; return false; }
    }
    memcpy(component, start, len);
    component[len] = 0;
    *cursor = p;
    return true;
}
