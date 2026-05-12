    const char *rw_path = "/disk0/aurora-rw.txt";
    const char rw_payload[] = "Aurora EXT4 write path survives VFS create/write/read/unlink";
    vfs_status_t rw_cleanup = vfs_unlink(rw_path);
    vfs_status_t rw_create = vfs_create(rw_path, rw_payload, sizeof(rw_payload) - 1u);
    if (rw_create != VFS_OK) {
        kprintf("[ detail] ext4 rw create cleanup=%s create=%s\n", vfs_status_name(rw_cleanup), vfs_status_name(rw_create));
    }
    check(rw_create == VFS_OK, "VFS create /disk0 file through ext4 writer");
    if (part) {
        ext4_mount_t verify_mnt;
        ext4_inode_disk_t created_inode;
        u32 created_ino = 0;
        bool extent_file = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &verify_mnt) == EXT4_OK &&
                           ext4_lookup_path(&verify_mnt, "/aurora-rw.txt", &created_inode, &created_ino) == EXT4_OK &&
                           created_ino != 0 && (created_inode.i_flags & EXT4_INODE_FLAG_EXTENTS) != 0 &&
                           ((const u16 *)created_inode.i_block)[0] == EXT4_EXTENT_MAGIC;
        check(extent_file, "EXT4 writer creates regular files with inline extent tree");
    } else {
        skip("EXT4 writer creates regular files with inline extent tree");
    }
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_read(rw_path, 0, text, sizeof(text) - 1u, &got) == VFS_OK && got == sizeof(rw_payload) - 1u && strstr(text, "EXT4 write") != 0, "VFS read back ext4-created file");
    const char patch[] = "persistent";
    usize wrote = 0;
    check(vfs_write(rw_path, 7u, patch, sizeof(patch) - 1u, &wrote) == VFS_OK && wrote == sizeof(patch) - 1u, "VFS overwrite ext4 file");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_read(rw_path, 0, text, sizeof(text) - 1u, &got) == VFS_OK && strstr(text, "persistent") != 0, "VFS read back ext4 overwrite");
    check(vfs_truncate(rw_path, 12u) == VFS_OK, "VFS truncate ext4 file smaller");
    vfs_stat_t trunc_st;
    check(vfs_stat(rw_path, &trunc_st) == VFS_OK && trunc_st.size == 12u, "VFS stat ext4 truncated size");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_read(rw_path, 0, text, sizeof(text) - 1u, &got) == VFS_OK && got == 12u, "VFS read ext4 truncated contents");
    check(vfs_truncate(rw_path, 8192u + 17u) == VFS_OK, "VFS expand ext4 file through truncate");
    check(vfs_stat(rw_path, &trunc_st) == VFS_OK && trunc_st.size == 8192u + 17u, "VFS stat ext4 expanded size");
    vfs_node_ref_t ext4_ref;
    check(vfs_get_ref(rw_path, &ext4_ref) == VFS_OK && ext4_ref.inode != 0, "VFS captures EXT4 inode ref before rename");
    const char *renamed_path = "/disk0/aurora-rw-renamed.txt";
    (void)vfs_unlink(renamed_path);
    check(vfs_rename(rw_path, renamed_path) == VFS_OK, "VFS rename ext4 file");
    check(vfs_stat(rw_path, &trunc_st) == VFS_ERR_NOENT && vfs_stat(renamed_path, &trunc_st) == VFS_OK && trunc_st.size == 8192u + 17u, "VFS observes ext4 renamed file");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_read_ref(&ext4_ref, 0, text, 12u, &got) == VFS_OK && got == 12u && memcmp(text, "Aurora persi", 12u) == 0, "VFS read_ref survives EXT4 rename");
    wrote = 0;
    check(vfs_write_ref(&ext4_ref, 0, "FD", 2u, &wrote) == VFS_OK && wrote == 2u, "VFS write_ref survives EXT4 rename");
    check(vfs_sync_ref(&ext4_ref, true) == VFS_OK && vfs_sync_ref(&ext4_ref, false) == VFS_OK, "VFS sync_ref EXT4 file data/full");
    check(vfs_sync_parent_dir(renamed_path) == VFS_OK, "VFS fsync parent dir after EXT4 rename");

    const char *replace_src = "/disk0/aurora-replace-src.txt";
    const char *replace_dst = "/disk0/aurora-replace-dst.txt";
    (void)vfs_unlink(replace_src);
    (void)vfs_unlink(replace_dst);
    check(vfs_create(replace_dst, "old-file", 8u) == VFS_OK && vfs_create(replace_src, "new-file", 8u) == VFS_OK, "VFS prepares ext4 rename-overwrite files");
    check(vfs_rename(replace_src, replace_dst) == VFS_OK, "VFS rename overwrites existing ext4 file atomically");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_stat(replace_src, &trunc_st) == VFS_ERR_NOENT && vfs_read(replace_dst, 0, text, 8u, &got) == VFS_OK && got == 8u && memcmp(text, "new-file", 8u) == 0, "VFS rename-overwrite exposes replacement contents");

    const char *install_stage = "/disk0/aurora-app.stage";
    const char *install_final = "/disk0/aurora-app.bin";
    (void)vfs_unlink(install_stage);
    (void)vfs_unlink(install_final);
    check(vfs_create(install_final, "app-v1", 6u) == VFS_OK && vfs_create(install_stage, "app-v2", 6u) == VFS_OK, "VFS prepares staged ext4 app install");
    check(vfs_install_commit(install_stage, install_final) == VFS_OK, "VFS install_commit swaps staged ext4 app into final path");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_stat(install_stage, &trunc_st) == VFS_ERR_NOENT && vfs_read(install_final, 0, text, 6u, &got) == VFS_OK && got == 6u && memcmp(text, "app-v2", 6u) == 0, "VFS install_commit preserves final app payload");
    check(vfs_sync_path(install_final) == VFS_OK, "VFS install_commit result fsyncs through EXT4");
    (void)vfs_unlink(install_final);
    (void)vfs_unlink(replace_dst);

    const char *ext_hl_src = "/disk0/aurora-hlink-src.txt";
    const char *ext_hl_dst = "/disk0/aurora-hlink-dst.txt";
    (void)vfs_unlink(ext_hl_dst);
    (void)vfs_unlink(ext_hl_src);
    check(vfs_create(ext_hl_src, "hard", 4u) == VFS_OK, "EXT4 hardlink source create");
    check(vfs_link(ext_hl_src, ext_hl_dst) == VFS_OK, "EXT4 hardlink create");
    vfs_stat_t ext_hl_src_st;
    vfs_stat_t ext_hl_dst_st;
    check(vfs_stat(ext_hl_src, &ext_hl_src_st) == VFS_OK && vfs_stat(ext_hl_dst, &ext_hl_dst_st) == VFS_OK && ext_hl_src_st.inode == ext_hl_dst_st.inode && ext_hl_src_st.nlink == 2u && ext_hl_dst_st.nlink == 2u, "EXT4 hardlink shares inode and link count");
    wrote = 0;
    check(vfs_write(ext_hl_dst, 0, "HARD", 4u, &wrote) == VFS_OK && wrote == 4u, "EXT4 hardlink write through alias");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_read(ext_hl_src, 0, text, sizeof(text) - 1u, &got) == VFS_OK && got == 4u && memcmp(text, "HARD", 4u) == 0, "EXT4 hardlink source sees alias write");
    check(vfs_unlink(ext_hl_src) == VFS_OK, "EXT4 unlink one hardlink");
    check(vfs_stat(ext_hl_dst, &ext_hl_dst_st) == VFS_OK && ext_hl_dst_st.nlink == 1u && ext_hl_dst_st.size == 4u, "EXT4 hardlink survives source unlink");
    check(vfs_unlink(ext_hl_dst) == VFS_OK, "EXT4 unlink final hardlink");

    const char *ext_sym_target = "/disk0/aurora-sym-target.txt";
    const char *ext_sym_path = "/disk0/aurora-sym.ln";
    (void)vfs_unlink(ext_sym_path);
    (void)vfs_unlink(ext_sym_target);
    check(vfs_create(ext_sym_target, "target", 6u) == VFS_OK, "EXT4 symlink target create");
    check(vfs_symlink("aurora-sym-target.txt", ext_sym_path) == VFS_OK, "EXT4 symlink create relative target");
    vfs_stat_t ext_sym_lst;
    check(vfs_lstat(ext_sym_path, &ext_sym_lst) == VFS_OK && ext_sym_lst.type == VFS_NODE_SYMLINK && ext_sym_lst.size == strlen("aurora-sym-target.txt") && ext_sym_lst.nlink == 1u, "EXT4 lstat sees symlink inode");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_readlink(ext_sym_path, text, sizeof(text), &got) == VFS_OK && got == strlen("aurora-sym-target.txt") && memcmp(text, "aurora-sym-target.txt", got) == 0, "EXT4 readlink returns raw target");
    check(vfs_stat(ext_sym_path, &trunc_st) == VFS_OK && trunc_st.type == VFS_NODE_FILE && trunc_st.size == 6u, "EXT4 stat follows symlink");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_read(ext_sym_path, 0, text, sizeof(text) - 1u, &got) == VFS_OK && got == 6u && memcmp(text, "target", 6u) == 0, "EXT4 read follows symlink");
    check(vfs_unlink(ext_sym_path) == VFS_OK && vfs_stat(ext_sym_target, &trunc_st) == VFS_OK, "EXT4 unlink symlink keeps target");

    const char *ext_sym2 = "/disk0/aurora-sym-hard.ln";
    const char *ext_sym_hard = "/disk0/aurora-sym-hard-alias.ln";
    (void)vfs_unlink(ext_sym_hard);
    (void)vfs_unlink(ext_sym2);
    check(vfs_symlink("aurora-sym-target.txt", ext_sym2) == VFS_OK, "EXT4 symlink hardlink source create");
    check(vfs_link(ext_sym2, ext_sym_hard) == VFS_OK, "EXT4 hardlink to symlink inode create");
    vfs_stat_t ext_sym2_st;
    vfs_stat_t ext_sym_hard_st;
    check(vfs_lstat(ext_sym2, &ext_sym2_st) == VFS_OK && vfs_lstat(ext_sym_hard, &ext_sym_hard_st) == VFS_OK && ext_sym2_st.type == VFS_NODE_SYMLINK && ext_sym2_st.inode == ext_sym_hard_st.inode && ext_sym2_st.nlink == 2u && ext_sym_hard_st.nlink == 2u, "EXT4 hardlink to symlink preserves symlink inode");
    check(vfs_unlink(ext_sym2) == VFS_OK, "EXT4 unlink original symlink hardlink");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_lstat(ext_sym_hard, &ext_sym_hard_st) == VFS_OK && ext_sym_hard_st.nlink == 1u && vfs_readlink(ext_sym_hard, text, sizeof(text), &got) == VFS_OK && got == strlen("aurora-sym-target.txt"), "EXT4 symlink hardlink survives unlink");
    check(vfs_unlink(ext_sym_hard) == VFS_OK && vfs_unlink(ext_sym_target) == VFS_OK, "EXT4 symlink hardlink cleanup");

    const char *ext_long_sym = "/disk0/aurora-long-sym.ln";
    const char ext_long_sym_target[] = "this/is/a/long/symlink/target/that/exercises/ext4-extent-backed-target-storage";
    (void)vfs_unlink(ext_long_sym);
    check(vfs_symlink(ext_long_sym_target, ext_long_sym) == VFS_OK, "EXT4 long symlink create");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_lstat(ext_long_sym, &ext_sym_lst) == VFS_OK && ext_sym_lst.type == VFS_NODE_SYMLINK && ext_sym_lst.size == strlen(ext_long_sym_target) && vfs_readlink(ext_long_sym, text, sizeof(text), &got) == VFS_OK && got == strlen(ext_long_sym_target) && memcmp(text, ext_long_sym_target, got) == 0, "EXT4 long symlink lstat/readlink");
    check(vfs_unlink(ext_long_sym) == VFS_OK, "EXT4 long symlink cleanup");

    (void)vfs_unlink("/disk0/aurora-dir/subdir");
    (void)vfs_unlink("/disk0/aurora-dir/child.txt");
    (void)vfs_unlink("/disk0/aurora-dir");
    check(vfs_mkdir("/disk0/aurora-dir") == VFS_OK, "VFS mkdir on ext4");
    vfs_stat_t ext_dir_before;
    vfs_stat_t ext_dir_after;
    check(vfs_stat("/disk0/aurora-dir", &ext_dir_before) == VFS_OK && ext_dir_before.type == VFS_NODE_DIR && ext_dir_before.nlink >= 2u, "EXT4 directory nlink baseline");
    check(vfs_mkdir("/disk0/aurora-dir/subdir") == VFS_OK, "EXT4 mkdir child directory for nlink");
    check(vfs_stat("/disk0/aurora-dir", &ext_dir_after) == VFS_OK && ext_dir_after.nlink == ext_dir_before.nlink + 1u, "EXT4 parent directory nlink increments on mkdir");
    check(vfs_unlink("/disk0/aurora-dir/subdir") == VFS_OK, "EXT4 unlink empty child directory for nlink");
    check(vfs_stat("/disk0/aurora-dir", &ext_dir_after) == VFS_OK && ext_dir_after.nlink == ext_dir_before.nlink, "EXT4 parent directory nlink decrements on rmdir");
    check(vfs_create("/disk0/aurora-dir/child.txt", "x", 1u) == VFS_OK, "VFS create child on ext4");
    check(vfs_unlink("/disk0/aurora-dir") == VFS_ERR_NOTEMPTY, "VFS rejects unlink non-empty ext4 dir");
    check(vfs_unlink("/disk0/aurora-dir/child.txt") == VFS_OK, "VFS unlink ext4 child");
    check(vfs_unlink("/disk0/aurora-dir") == VFS_OK, "VFS unlink empty ext4 dir");

