#include <rabbitbone_sys.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    const char msg[] = "hello from Rabbitbone user mode via int80\n";
    au_write_console(msg, sizeof(msg) - 1);
    au_log("/bin/hello reached user mode and returned through SYS_EXIT");
    return 7;
}
