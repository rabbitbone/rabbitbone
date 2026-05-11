#include <aurora_sys.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    int opened = 0;
    for (int i = 0; i < (int)AURORA_PROCESS_HANDLE_CAP - 3; ++i) {
        au_i64 h = au_open("/etc/motd");
        if (h <= 0) return 10 + i;
        ++opened;
    }
    if (opened != (int)AURORA_PROCESS_HANDLE_CAP - 3) return 2;
    return 0;
}
