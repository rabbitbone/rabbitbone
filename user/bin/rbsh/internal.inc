#define SH_LINE_MAX 1024u
#define SH_TOKENS_MAX 64u
#define SH_STAGES_MAX 8u
#define SH_ARG_MAX 32u
#define SH_TOKEN_MAX 256u
#define SH_PATH_MAX RABBITBONE_PATH_MAX
#define SH_DEFAULT_PATH "/disk0/bin:/disk0/sbin:/bin:/sbin"
#define SH_ENV_FILE "/etc/profile"
#define SH_SCAN_PID_MAX 256u
#define SH_PATH_PARSE_BUF 768u
#define SH_VAR_MAX 24u
#define SH_VAR_NAME_MAX 32u
#define SH_ENV_ENTRY_MAX 288u
#define SH_ASSIGN_MAX 8u

#if defined(__clang__) || defined(__GNUC__)
#define SH_NOINLINE __attribute__((noinline))
#else
#define SH_NOINLINE
#endif

#define SH_NODE_FILE 1u
#define SH_NODE_DIR 2u
#define SH_NODE_DEV 3u
#define SH_NODE_SYMLINK 4u

#define SH_OP_SEQ 0u
#define SH_OP_AND 1u
#define SH_OP_OR 2u

#define SH_REDIR_NONE 0u
#define SH_REDIR_TRUNC 1u
#define SH_REDIR_APPEND 2u
#define SH_REDIR_DUP 3u

static unsigned int sh_last_async_pid = 0;
static int sh_last_status = 0;
static unsigned int sh_shell_pid = 0;
static int sh_interactive_shell = 0;
static int sh_login_shell = 0;
static char sh_path_env[SH_PATH_MAX];
static int sh_path_env_loaded = 0;

typedef struct sh_var {
    char name[SH_VAR_NAME_MAX];
    char value[SH_TOKEN_MAX];
    unsigned int exported;
} sh_var_t;

static sh_var_t sh_vars[SH_VAR_MAX];


typedef struct sh_stage {
    char *argv[SH_ARG_MAX];
    unsigned int argc;
    char token_storage[SH_ARG_MAX][SH_TOKEN_MAX];
    char assign_name[SH_ASSIGN_MAX][SH_VAR_NAME_MAX];
    char assign_value[SH_ASSIGN_MAX][SH_TOKEN_MAX];
    unsigned int assign_count;
    char path[SH_PATH_MAX];
    char in_path[SH_PATH_MAX];
    char out_path[SH_PATH_MAX];
    char err_path[SH_PATH_MAX];
    unsigned int out_mode;
    unsigned int err_mode;
    unsigned int err_to_out;
    unsigned int out_to_err;
} sh_stage_t;

static sh_stage_t sh_pipeline_stages[SH_STAGES_MAX];
static sh_stage_t sh_command_stage;
static char sh_parse_tokens[SH_TOKENS_MAX][SH_TOKEN_MAX];
static char sh_repl_line[SH_LINE_MAX];
static char sh_exec_line[SH_LINE_MAX];
