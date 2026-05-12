static void test_block_mbr_ext4(void) {
#include "ext4_disk/disk_probe_read.inc"
#include "ext4_disk/rw_rename_install_links_symlinks_dirs.inc"
#include "ext4_disk/sparse_cache_prealloc.inc"
#include "ext4_disk/indexed_extents.inc"
#include "ext4_disk/split_depth2_extents.inc"
#include "ext4_disk/htree_repair.inc"
#include "ext4_disk/dirent_repair_final.inc"
}
