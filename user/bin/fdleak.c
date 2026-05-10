#include <aurora_sys.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    int opened = 0;
    for (int i = 0; i < 31; ++i) {
        au_i64 h = au_open("/etc/motd");
        if (h <= 0) return 10 + i;
        ++opened;
    }
    if (opened != 31) return 2;
    return 0;
}
