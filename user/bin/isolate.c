#include <aurora_sys.h>

static unsigned long long canary;
static char local_buf[64];

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    if (canary != 0) return 1;
    for (unsigned i = 0; i < sizeof(local_buf); ++i) {
        if (local_buf[i] != 0) return 2;
        local_buf[i] = (char)(0x40 + i);
    }
    canary = 0x1122334455667788ull;
    return 41;
}
