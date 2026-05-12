typedef struct sys_user_record {
    const char *name;
    const char *password;
    u32 uid;
    u32 gid;
    bool admin;
} sys_user_record_t;

static const sys_user_record_t auth_users[] = {
    { "root", "root", AURORA_UID_ROOT, AURORA_GID_ROOT, true },
    { "aurora", "aurora", AURORA_UID_AURORA, AURORA_GID_USERS, true },
    { "guest", "guest", AURORA_UID_GUEST, AURORA_GID_USERS, false },
};

static const sys_user_record_t *auth_find_user_name(const char *name) {
    if (!name || !name[0]) return 0;
    for (usize i = 0; i < sizeof(auth_users) / sizeof(auth_users[0]); ++i) {
        if (strcmp(auth_users[i].name, name) == 0) return &auth_users[i];
    }
    return 0;
}

static const sys_user_record_t *auth_find_user_uid(u32 uid) {
    for (usize i = 0; i < sizeof(auth_users) / sizeof(auth_users[0]); ++i) {
        if (auth_users[i].uid == uid) return &auth_users[i];
    }
    return 0;
}

static bool auth_password_ok(const sys_user_record_t *u, const char *password) {
    return u && password && strcmp(u->password, password) == 0;
}

static void auth_fill_cred(aurora_credinfo_t *cred, const sys_user_record_t *u) {
    if (!cred || !u) return;
    memset(cred, 0, sizeof(*cred));
    cred->uid = u->uid;
    cred->euid = u->uid;
    cred->gid = u->gid;
    cred->egid = u->gid;
    cred->is_admin = u->admin ? 1u : 0u;
    cred->umask = 022u;
    cred->sudo_ttl = AURORA_SUDO_DEFAULT_TTL_TICKS;
    strncpy(cred->user, u->name, sizeof(cred->user) - 1u);
}

static bool auth_cache_valid(const aurora_credinfo_t *cred) {
    return cred && cred->sudo_until > pit_ticks();
}

syscall_result_t aurora_sys_cred(u64 op64, u64 arg0, u64 arg1, u64 value64) {
    u32 op = (u32)op64;
    if (op64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    if (op == AURORA_CRED_OP_GET) {
        aurora_credinfo_t cred;
        if (!process_current_credentials(&cred)) return err(VFS_ERR_INVAL);
        cred.sudo_cached = auth_cache_valid(&cred) ? 1u : 0u;
        return copy_out_buf(arg0, &cred, sizeof(cred)) ? ok(0) : err(VFS_ERR_INVAL);
    }
    if (op == AURORA_CRED_OP_USERINFO) {
        if (value64 >= sizeof(auth_users) / sizeof(auth_users[0])) return err(VFS_ERR_NOENT);
        const sys_user_record_t *u = &auth_users[value64];
        aurora_userinfo_t info;
        memset(&info, 0, sizeof(info));
        info.uid = u->uid;
        info.gid = u->gid;
        info.is_admin = u->admin ? 1u : 0u;
        strncpy(info.user, u->name, sizeof(info.user) - 1u);
        return copy_out_buf(arg0, &info, sizeof(info)) ? ok(0) : err(VFS_ERR_INVAL);
    }
    if (op == AURORA_CRED_OP_LOGIN) {
        char name[AURORA_USER_NAME_MAX];
        char password[64];
        if (!copy_string_arg(arg0, name, sizeof(name)) || !copy_string_arg(arg1, password, sizeof(password))) return err(VFS_ERR_INVAL);
        const sys_user_record_t *u = auth_find_user_name(name);
        if (!auth_password_ok(u, password)) return err(VFS_ERR_PERM);
        aurora_credinfo_t cred;
        auth_fill_cred(&cred, u);
        return process_set_current_credentials(&cred) ? ok(0) : err(VFS_ERR_INVAL);
    }
    if (op == AURORA_CRED_OP_SET_USER) {
        aurora_credinfo_t cur;
        if (!process_current_credentials(&cur) || cur.euid != AURORA_UID_ROOT) return err(VFS_ERR_PERM);
        char name[AURORA_USER_NAME_MAX];
        if (!copy_string_arg(arg0, name, sizeof(name))) return err(VFS_ERR_INVAL);
        const sys_user_record_t *u = auth_find_user_name(name);
        if (!u) return err(VFS_ERR_NOENT);
        aurora_credinfo_t next;
        auth_fill_cred(&next, u);
        return process_set_current_credentials(&next) ? ok(0) : err(VFS_ERR_INVAL);
    }
    if (op == AURORA_CRED_OP_SET_EUID) {
        aurora_credinfo_t cur;
        if (!process_current_credentials(&cur)) return err(VFS_ERR_INVAL);
        u32 euid = (u32)value64;
        if (value64 > 0xffffffffull) return err(VFS_ERR_INVAL);
        if (cur.euid == AURORA_UID_ROOT || euid == cur.uid || (euid == AURORA_UID_ROOT && cur.is_admin && auth_cache_valid(&cur))) {
            return process_set_current_euid(euid) ? ok(0) : err(VFS_ERR_INVAL);
        }
        return err(VFS_ERR_PERM);
    }
    return err(VFS_ERR_INVAL);
}

syscall_result_t aurora_sys_sudo(u64 op64, u64 password_ptr, u64 value64) {
    if (op64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    u32 op = (u32)op64;
    aurora_credinfo_t cred;
    if (!process_current_credentials(&cred)) return err(VFS_ERR_INVAL);
    const sys_user_record_t *u = auth_find_user_uid(cred.uid);
    if (!u || !u->admin) return err(VFS_ERR_PERM);
    u64 now = pit_ticks();
    if (op == AURORA_SUDO_OP_STATUS) return ok(auth_cache_valid(&cred) ? 1 : 0);
    if (op == AURORA_SUDO_OP_DROP) {
        cred.euid = cred.uid;
        cred.egid = cred.gid;
        cred.sudo_persistent = 0;
        return process_set_current_credentials(&cred) ? ok(0) : err(VFS_ERR_INVAL);
    }
    if (op == AURORA_SUDO_OP_INVALIDATE) {
        cred.euid = cred.uid;
        cred.egid = cred.gid;
        cred.sudo_until = 0;
        cred.sudo_persistent = 0;
        return process_set_current_credentials(&cred) ? ok(0) : err(VFS_ERR_INVAL);
    }
    if (op == AURORA_SUDO_OP_SET_TIMEOUT) {
        if (value64 > AURORA_SUDO_MAX_TTL_TICKS) return err(VFS_ERR_INVAL);
        cred.sudo_ttl = value64;
        return process_set_current_credentials(&cred) ? ok(0) : err(VFS_ERR_INVAL);
    }
    if (op == AURORA_SUDO_OP_VALIDATE) {
        bool okpass = false;
        if (password_ptr) {
            char password[64];
            if (!copy_string_arg(password_ptr, password, sizeof(password))) return err(VFS_ERR_INVAL);
            okpass = auth_password_ok(u, password);
        } else {
            okpass = auth_cache_valid(&cred);
        }
        if (!okpass) return err(VFS_ERR_PERM);
        u64 ttl = cred.sudo_ttl ? cred.sudo_ttl : AURORA_SUDO_DEFAULT_TTL_TICKS;
        if (ttl > AURORA_SUDO_MAX_TTL_TICKS) ttl = AURORA_SUDO_MAX_TTL_TICKS;
        cred.sudo_until = now + ttl;
        cred.sudo_cached = 1u;
        u32 flags = (u32)value64;
        if (flags & AURORA_SUDO_FLAG_ACTIVATE) {
            cred.euid = AURORA_UID_ROOT;
            cred.egid = AURORA_GID_ROOT;
        }
        if (flags & AURORA_SUDO_FLAG_PERSIST) {
            cred.euid = AURORA_UID_ROOT;
            cred.egid = AURORA_GID_ROOT;
            cred.sudo_persistent = 1u;
        }
        return process_set_current_credentials(&cred) ? ok(0) : err(VFS_ERR_INVAL);
    }
    return err(VFS_ERR_INVAL);
}
