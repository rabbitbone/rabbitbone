#include <aurora_sys.h>

static int has_env(char **envp, const char *needle) {
    if (!envp || !needle) return 0;
    for (int i = 0; envp[i] && i < 16; ++i) {
        if (au_strcmp(envp[i], needle) == 0) return 1;
    }
    return 0;
}

int main(int argc, char **argv, char **envp) {
    if (!argv || argc < 2) return 10;
    if (au_strcmp(argv[1], "--opt") == 0) {
        if (argc != 5) return 20;
        if (au_strcmp(argv[0], "/bin/shebangcheck") != 0) return 21;
        if (au_strcmp(argv[2], "/disk0/aurora-shebang-opt.sh") != 0) return 22;
        if (au_strcmp(argv[3], "alpha") != 0) return 23;
        if (au_strcmp(argv[4], "beta") != 0) return 24;
        return 0;
    }
    if (au_strcmp(argv[1], "/disk0/aurora-shebang-basic.sh") == 0) {
        if (argc != 3) return 30;
        if (au_strcmp(argv[0], "/bin/shebangcheck") != 0) return 31;
        if (au_strcmp(argv[2], "solo") != 0) return 32;
        return 0;
    }
    if (au_strcmp(argv[1], "--env") == 0) {
        if (argc != 3) return 40;
        if (au_strcmp(argv[0], "/bin/shebangcheck") != 0) return 41;
        if (au_strcmp(argv[2], "/disk0/aurora-shebang-env.sh") != 0) return 42;
        if (!has_env(envp, "SHEBANG_ENV=ok")) return 43;
        if (!has_env(envp, "UNCHANGED=yes")) return 44;
        return 0;
    }
    if (au_strcmp(argv[1], "--exec") == 0) {
        if (argc != 4) return 50;
        if (au_strcmp(argv[0], "/bin/shebangcheck") != 0) return 51;
        if (au_strcmp(argv[2], "/disk0/aurora-shebang-exec.sh") != 0) return 52;
        if (au_strcmp(argv[3], "from-exec") != 0) return 53;
        return 0;
    }
    return 60;
}
