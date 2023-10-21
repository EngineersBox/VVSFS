#include <asm/uaccess.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mpage.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/types.h>
#include <linux/version.h>

#include "vvsfs.h"
#include "buffer_utils.h"
#include "logging.h"

// This implements the super operation for writing a
// 'dirty' inode to disk Note that this does not sync
// the actual data blocks pointed to by the inode; it
// only saves the meta data (e.g., the data block
// pointers, but not the actual data contained in the
// data blocks). Data blocks sync is taken care of by
// file and directory operations.
static int vvsfs_write_inode(struct inode *inode,
                             struct writeback_control *wbc) {
    struct super_block *sb;
    struct vvsfs_inode *disk_inode;
    struct vvsfs_inode_info *inode_info;
    struct buffer_head *bh;
    uint32_t inode_block, inode_offset;
    int i;

    // LOG("vvsfs - write_inode");

    // get the vvsfs_inode_info associated with this
    // (VFS) inode from cache.
    inode_info = VVSFS_I(inode);

    sb = inode->i_sb;
    inode_block = vvsfs_get_inode_block(inode->i_ino);
    inode_offset = vvsfs_get_inode_offset(inode->i_ino);

    /*LOG("vvsfs - write_inode - ino: %ld, block: "*/
    /*"%d, offset: %d",*/
    /*inode->i_ino,*/
    /*inode_block,*/
    /*inode_offset);*/
    bh = sb_bread(sb, inode_block);
    if (!bh)
        return -EIO;

    disk_inode = (struct vvsfs_inode *)((uint8_t *)bh->b_data + inode_offset);
    disk_inode->i_mode = inode->i_mode;
    disk_inode->i_uid = i_uid_read(inode);
    disk_inode->i_gid = i_gid_read(inode);
    disk_inode->i_size = inode->i_size;
    disk_inode->i_atime = inode->i_atime.tv_sec;
    disk_inode->i_mtime = inode->i_mtime.tv_sec;
    disk_inode->i_ctime = inode->i_ctime.tv_sec;
    disk_inode->i_data_blocks_count = inode_info->i_db_count;
    disk_inode->i_links_count = inode->i_nlink;
    disk_inode->i_rdev = inode->i_rdev;
    for (i = 0; i < VVSFS_N_BLOCKS; ++i)
        disk_inode->i_block[i] = inode_info->i_data[i];

    // TODO: if you have additional data added to the
    // on-disk inode structure, you need to sync it
    // here.

    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    // LOG("vvsfs - write_inode done: %ld\n", inode->i_ino);
    return 0;
}

// This implements the super operation to allocate a
// new inode. This will be called everytime a request
// to allocate a new (in memory) inode is made. By
// default, this is handled by VFS, which allocates an
// VFS inode structure. But we override it here so
// that we can attach filesystem-specific information
// (.e.g, pointers to data blocks). It is unlikely
// that you will need to modify this function
// directly; you can simply add whatever additional
// information you want to attach to the
// vvsfs_inode_info structure.
static struct inode *vvsfs_alloc_inode(struct super_block *sb) {
    struct vvsfs_inode_info *c_inode =
        kmem_cache_alloc(vvsfs_inode_cache, GFP_KERNEL);

    LOG("vvsfs - alloc_inode ");

    if (!c_inode)
        return NULL;

    inode_init_once(&c_inode->vfs_inode);
    return &c_inode->vfs_inode;
}

// Deallocate the inode cache.
static void vvsfs_destroy_inode(struct inode *inode) {
    struct vvsfs_inode_info *c_inode =
        container_of(inode, struct vvsfs_inode_info, vfs_inode);
    kmem_cache_free(vvsfs_inode_cache, c_inode);
}

// put_super is part of the super operations. This
// is called when the filesystem is being unmounted,
// to deallocate memory space taken up by the super
// block info.
static void vvsfs_put_super(struct super_block *sb) {
    struct vvsfs_sb_info *sbi = sb->s_fs_info;

    LOG("vvsfs - put_super\n");

    if (sbi) {
        kfree(sbi->imap);
        kfree(sbi->dmap);
        kfree(sbi);
    }
}

static uint32_t count_free(uint8_t *map, uint32_t size) {
    int i;
    uint8_t j;
    uint32_t count = 0;
    for (i = 0; i < size; i++) {
        for (j = 0; j < 8; j++) {
            if (i == 0 && j == 0) {
                continue; // Block 0 is reserved
            } else if ((~map[i]) & (VVSFS_SET_MAP_BIT >> j)) {
                count++;
            }
        }
    }
    return count;
}

// statfs -- this is currently incomplete.
// See
// https://elixir.bootlin.com/linux/v5.15.89/source/fs/ext2/super.c#L1407
// for various stats that you need to provide.
static int vvsfs_statfs(struct dentry *dentry, struct kstatfs *buf) {
    struct super_block *sb;
    struct vvsfs_sb_info *i_sb;
    uint64_t id;
    LOG("vvsfs - statfs\n");
    sb = dentry->d_sb;
    i_sb = sb->s_fs_info;
    // Retrieve the device id from the superblock device data
    id = huge_encode_dev(sb->s_bdev->bd_dev);
    // Convert the raw device id to __kernel_fsid_t
    buf->f_fsid = u64_to_fsid(id);
    buf->f_blocks = i_sb->nblocks;
    buf->f_bfree = count_free(i_sb->dmap, VVSFS_DMAP_SIZE);
    // We don't have any privilege scoped block access
    // behaviour so bavail is the same as bfree
    buf->f_bavail = buf->f_bfree;
    buf->f_files = i_sb->ninodes;
    buf->f_ffree = count_free(i_sb->imap, VVSFS_IMAP_SIZE);
    buf->f_namelen = VVSFS_MAXNAME;
    buf->f_type = VVSFS_MAGIC;
    buf->f_bsize = VVSFS_BLOCKSIZE;
    LOG("vvsfs - statfs - done\n");
    return 0;
}

// Fill the super_block structure with information
// specific to vvsfs
static int vvsfs_fill_super(struct super_block *s, void *data, int silent) {
    struct inode *root_inode;
    int hblock;
    struct buffer_head *bh;
    uint32_t magic;
    struct vvsfs_sb_info *sbi;

    LOG("vvsfs - fill super\n");

    s->s_flags = ST_NOSUID | SB_NOEXEC;
    s->s_op = &vvsfs_ops;
    s->s_magic = VVSFS_MAGIC;

    hblock = bdev_logical_block_size(s->s_bdev);
    if (hblock > VVSFS_BLOCKSIZE) {
        LOG("vvsfs - device blocks are too small!!");
        return -1;
    }

    sb_set_blocksize(s, VVSFS_BLOCKSIZE);

    /* Read first block of the superblock.
        For this basic version, it contains just the
       magic number. */

    bh = sb_bread(s, 0);
    magic = *((uint32_t *)bh->b_data);
    if (magic != VVSFS_MAGIC) {
        LOG("vvsfs - wrong magic number\n");
        return -EINVAL;
    }
    brelse(bh);

    /* Allocate super block info to load inode & data
     * map */
    sbi = kzalloc(sizeof(struct vvsfs_sb_info), GFP_KERNEL);
    if (!sbi) {
        LOG("vvsfs - error allocating vvsfs_sb_info");
        return -ENOMEM;
    }

    /* Set max supported blocks */
    sbi->nblocks = VVSFS_MAXBLOCKS;
    /* Set max supported inodes */
    sbi->ninodes = VVSFS_MAX_INODE_ENTRIES;

    /* Load the inode map */
    sbi->imap = kzalloc(VVSFS_IMAP_SIZE, GFP_KERNEL);
    if (!sbi->imap)
        return -ENOMEM;
    bh = sb_bread(s, 1);
    if (!bh)
        return -EIO;
    memcpy(sbi->imap, bh->b_data, VVSFS_IMAP_SIZE);
    brelse(bh);

    /* Load the data map. Note that the data map
     * occupies 2 blocks.  */
    sbi->dmap = kzalloc(VVSFS_DMAP_SIZE, GFP_KERNEL);
    if (!sbi->dmap)
        return -ENOMEM;
    bh = sb_bread(s, 2);
    if (!bh)
        return -EIO;
    memcpy(sbi->dmap, bh->b_data, VVSFS_BLOCKSIZE);
    brelse(bh);
    bh = sb_bread(s, 3);
    if (!bh)
        return -EIO;
    memcpy(sbi->dmap + VVSFS_BLOCKSIZE, bh->b_data, VVSFS_BLOCKSIZE);
    brelse(bh);

    /* Attach the bitmaps to the in-memory super_block
     * s */
    s->s_fs_info = sbi;

    /* Read the root inode from disk */
    root_inode = vvsfs_iget(s, 1);

    if (IS_ERR(root_inode)) {
        LOG("vvsfs - fill_super - error getting "
            "root inode");
        return PTR_ERR(root_inode);
    }

    /* Initialise the owner */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
    inode_init_owner(&init_user_ns, root_inode, NULL, root_inode->i_mode);
#else
    inode_init_owner(&nop_mnt_idmap, root_inode, NULL, root_inode->i_mode);
#endif
    mark_inode_dirty(root_inode);

    s->s_root = d_make_root(root_inode);

    if (!s->s_root) {
        LOG("vvsfs - fill_super - failed setting "
            "up root directory");
        iput(root_inode);
        return -ENOMEM;
    }

    LOG("vvsfs - fill super done\n");

    return 0;
}

// sync_fs super operation.
// This writes super block data to disk.
// For the current version, this is mainly the inode
// map and the data map.
static int vvsfs_sync_fs(struct super_block *sb, int wait) {
    struct vvsfs_sb_info *sbi = sb->s_fs_info;
    struct buffer_head *bh;

    LOG("vvsfs -- sync_fs");

    /* Write the inode map to disk */
    bh = sb_bread(sb, 1);
    if (!bh)
        return -EIO;
    memcpy(bh->b_data, sbi->imap, VVSFS_IMAP_SIZE);
    mark_buffer_dirty(bh);
    if (wait)
        sync_dirty_buffer(bh);
    brelse(bh);

    /* Write the data map */

    bh = sb_bread(sb, 2);
    if (!bh)
        return -EIO;
    memcpy(bh->b_data, sbi->dmap, VVSFS_BLOCKSIZE);
    mark_buffer_dirty(bh);
    if (wait)
        sync_dirty_buffer(bh);
    brelse(bh);

    bh = sb_bread(sb, 3);
    if (!bh)
        return -EIO;
    memcpy(bh->b_data, sbi->dmap + VVSFS_BLOCKSIZE, VVSFS_BLOCKSIZE);
    mark_buffer_dirty(bh);
    if (wait)
        sync_dirty_buffer(bh);
    brelse(bh);

    return 0;
}

static const struct super_operations vvsfs_ops = {
    .statfs = vvsfs_statfs,
    .put_super = vvsfs_put_super,
    .alloc_inode = vvsfs_alloc_inode,
    .destroy_inode = vvsfs_destroy_inode,
    .write_inode = vvsfs_write_inode,
    .sync_fs = vvsfs_sync_fs,
};
