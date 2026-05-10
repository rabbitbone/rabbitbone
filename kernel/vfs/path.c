#include <aurora/path.h>
#include <aurora/vfs.h>
#include <aurora/libc.h>

bool path_is_absolute(const char *path) { return path && path[0] == '/'; }

static bool push_component(char comps[][VFS_NAME_MAX], usize *count, const char *comp, usize len) {
    if (len == 0 || (len == 1 && comp[0] == '.')) return true;
    if (len == 2 && comp[0] == '.' && comp[1] == '.') {
        if (*count > 0) --*count;
        return true;
    }
    if (len >= VFS_NAME_MAX || *count >= 32u) return false;
    memcpy(comps[*count], comp, len);
    comps[*count][len] = 0;
    ++*count;
    return true;
}

bool path_normalize(const char *in, char *out, usize out_size) {
    if (!in || !out || out_size < 2u) return false;
    char comps[32][VFS_NAME_MAX];
    usize count = 0;
    const char *p = in;
    while (*p) {
        while (*p == '/') ++p;
        const char *start = p;
        while (*p && *p != '/') ++p;
        if (!push_component(comps, &count, start, (usize)(p - start))) return false;
    }
    usize pos = 0;
    out[pos++] = '/';
    for (usize i = 0; i < count; ++i) {
        usize len = strlen(comps[i]);
        if (pos + len + (i + 1u < count ? 1u : 0u) + 1u > out_size) return false;
        memcpy(out + pos, comps[i], len);
        pos += len;
        if (i + 1u < count) out[pos++] = '/';
    }
    out[pos] = 0;
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
    char tmp[VFS_PATH_MAX * 2u];
    usize bl = strlen(base);
    usize ll = strlen(leaf);
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
    const char *p = *cursor;
    while (*p == '/') ++p;
    if (!*p) { *cursor = p; component[0] = 0; return false; }
    const char *start = p;
    while (*p && *p != '/') ++p;
    usize len = (usize)(p - start);
    if (len >= component_size) { component[0] = 0; *cursor = p; return false; }
    memcpy(component, start, len);
    component[len] = 0;
    *cursor = p;
    return true;
}
