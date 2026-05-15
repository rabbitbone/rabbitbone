static void tar_octal(char *dst, usize width, usize value) {
    memset(dst, '0', width);
    dst[width - 1u] = 0;
    if (width < 2u) return;
    for (usize pos = width - 2u;; --pos) {
        dst[pos] = (char)('0' + (value & 7u));
        value >>= 3u;
        if (pos == 0 || value == 0) break;
    }
}

static void tar_finalize_checksum(u8 *block) {
    memset(block + 148, ' ', 8u);
    usize sum = 0;
    for (usize i = 0; i < 512u; ++i) sum += block[i];
    tar_octal((char *)block + 148, 8u, sum);
    block[155] = ' ';
}

static void tar_header_write(u8 *block, const char *name, const char *data, usize size) {
    memset(block, 0, 512u);
    strncpy((char *)block + 0, name, 99u);
    tar_octal((char *)block + 100, 8u, 0644u);
    tar_octal((char *)block + 108, 8u, 0u);
    tar_octal((char *)block + 116, 8u, 0u);
    tar_octal((char *)block + 124, 12u, size);
    tar_octal((char *)block + 136, 12u, 1u);
    block[156] = '0';
    memcpy(block + 257, "ustar", 5u);
    memcpy(block + 263, "00", 2u);
    tar_finalize_checksum(block);
    memcpy(block + 512u, data, size);
}

static void test_tarfs_module(void) {
    suite_begin("tarfs module");
    static const char data[] = "tarfs payload\n";
    u8 *image = (u8 *)kcalloc(1, 2048u);
    if (!image) {
        fail("allocate tar image");
        suite_end();
        return;
    }
    tar_header_write(image, "./boot/./test.txt", data, sizeof(data) - 1u);
    tarfs_t *fs = tarfs_open(image, 2048u);
    check(fs != 0, "tarfs opens ustar image");
    if (fs) {
        vfs_status_t ms = vfs_mount("/tar-test", "tarfs", tarfs_ops(), fs, false);
        check(ms == VFS_OK, "tarfs mounts through VFS");
        if (ms == VFS_OK) {
            vfs_stat_t st;
            check(vfs_stat("/tar-test/boot", &st) == VFS_OK && st.type == VFS_NODE_DIR, "tarfs implicit directory stat");
            char buf[32];
            usize got = 0;
            memset(buf, 0, sizeof(buf));
            check(vfs_read("/tar-test/boot/test.txt", 0, buf, sizeof(buf) - 1u, &got) == VFS_OK && strcmp(buf, data) == 0, "tarfs canonicalizes ./ components and reads through VFS");
            seen_name_ctx_t seen = { "test.txt", false };
            check(vfs_list("/tar-test/boot", list_seen_name, &seen) == VFS_OK && seen.seen, "tarfs directory list");
            check(vfs_write("/tar-test/boot/test.txt", 0, "x", 1u, 0) == VFS_ERR_PERM, "tarfs is read-only in VFS");
            check(vfs_unmount("/tar-test") == VFS_OK, "tarfs unmount");
        }
        tarfs_destroy(fs);
    }
    u8 *bad = (u8 *)kcalloc(1, 1024u);
    if (bad) {
        memcpy(bad, image, 1024u);
        memset(bad + 148, '0', 8u);
        check(tarfs_open(bad, 1024u) == 0, "tarfs rejects zero checksum header");
        memcpy(bad, image, 1024u);
        tar_octal((char *)bad + 124, 12u, 900u);
        memset(bad + 148, ' ', 8u);
        usize sum = 0;
        for (usize i = 0; i < 512u; ++i) sum += bad[i];
        tar_octal((char *)bad + 148, 8u, sum);
        bad[155] = ' ';
        check(tarfs_open(bad, 1024u) == 0, "tarfs rejects payload beyond image");
        kfree(bad);
    } else {
        skip("tarfs malformed image tests");
        skip("tarfs malformed image tests");
    }

    u8 *conflict = (u8 *)kcalloc(1, 4096u);
    if (conflict) {
        tar_header_write(conflict, "conflict", "x", 1u);
        tar_header_write(conflict + 1024u, "conflict/child.txt", "y", 1u);
        check(tarfs_open(conflict, 4096u) == 0, "tarfs rejects file/directory prefix conflicts");
        memset(conflict, 0, 4096u);
        tar_header_write(conflict, "dir", "x", 1u);
        conflict[156] = '5';
        tar_finalize_checksum(conflict);
        check(tarfs_open(conflict, 2048u) == 0, "tarfs rejects directory entries with payload data");
        kfree(conflict);
    } else {
        skip("tarfs prefix conflict malformed image test");
        skip("tarfs directory payload malformed image test");
    }


    u8 *policy = (u8 *)kcalloc(1, 4096u);
    if (policy) {
        tar_header_write(policy, "./", "", 0u);
        policy[156] = '5';
        tar_finalize_checksum(policy);
        check(tarfs_open(policy, 2048u) == 0, "tarfs rejects root-only ./ entry");

        memset(policy, 0, 4096u);
        tar_header_write(policy, "same.txt", "a", 1u);
        tar_header_write(policy + 1024u, "./same.txt", "b", 1u);
        check(tarfs_open(policy, 4096u) == 0, "tarfs rejects canonical duplicate names");

        memset(policy, 0, 4096u);
        tar_header_write(policy, "bad\\name", "x", 1u);
        check(tarfs_open(policy, 2048u) == 0, "tarfs rejects unsafe archive name bytes");
        kfree(policy);
    } else {
        skip("tarfs root-only ./ malformed image test");
        skip("tarfs canonical duplicate malformed image test");
        skip("tarfs unsafe name malformed image test");
    }

    tarfs_t *parent_fs = tarfs_open(image, 2048u);
    tarfs_t *child_fs = tarfs_open(image, 2048u);
    if (parent_fs && child_fs) {
        bool mounted_parent = vfs_mount("/tar-parent", "tarfs", tarfs_ops(), parent_fs, false) == VFS_OK;
        bool mounted_child = vfs_mount("/tar-parent/child", "tarfs", tarfs_ops(), child_fs, false) == VFS_OK;
        check(mounted_parent && mounted_child && vfs_unmount("/tar-parent") == VFS_ERR_NOTEMPTY, "vfs rejects unmount while child mount is active");
        if (mounted_child) (void)vfs_unmount("/tar-parent/child");
        if (mounted_parent) (void)vfs_unmount("/tar-parent");
    } else {
        skip("vfs nested mount unmount guard");
    }
    if (parent_fs) tarfs_destroy(parent_fs);
    if (child_fs) tarfs_destroy(child_fs);
    kfree(image);
    suite_end();
}

