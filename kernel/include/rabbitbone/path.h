#ifndef RABBITBONE_PATH_H
#define RABBITBONE_PATH_H
#include <rabbitbone/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define RABBITBONE_PATH_MAX 256u

bool path_is_absolute(const char *path);
bool path_normalize(const char *in, char *out, usize out_size);
const char *path_basename(const char *path);
bool path_join(const char *base, const char *leaf, char *out, usize out_size);
bool path_next_component(const char **cursor, char *component, usize component_size);

#if defined(__cplusplus)
}
#endif
#endif
