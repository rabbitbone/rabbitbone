#define SH_LINE_MAX 256u
#define SH_TOKENS_MAX 32u
#define SH_STAGES_MAX 6u
#define SH_ARG_MAX 16u
#define SH_PATH_MAX AURORA_PATH_MAX
#define SH_DEFAULT_PATH "/disk0/bin:/disk0/sbin:/bin:/sbin"
#define SH_ENV_FILE "/etc/profile"
#define SH_SCAN_PID_MAX 256u
#define SH_PATH_PARSE_BUF 512u

#define SH_NODE_FILE 1u
#define SH_NODE_DIR 2u
#define SH_NODE_DEV 3u
#define SH_NODE_SYMLINK 4u

static unsigned int sh_last_async_pid = 0;
static char sh_path_env[SH_PATH_MAX];
static int sh_path_env_loaded = 0;
