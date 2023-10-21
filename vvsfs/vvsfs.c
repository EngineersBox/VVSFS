/*
 * The Very Very Simple File System (vvsfs)
 * Eric McCreath 2006, 2008, 2010, 2020, 2023 - GPL
 * (based on the simplistic RAM filesystem McCreath 2001)
 *
 * Alwen Tiu, 2023. Added various improvements to allow multiple data blocks,
 * bitmaps, and using address space operations to simplify read/write.
 */

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

// inode cache -- this is used to attach vvsfs specific inode
// data to the vfs inode
static struct kmem_cache *vvsfs_inode_cache;

struct inode *vvsfs_iget(struct super_block *sb, unsigned long ino);
static int vvsfs_free_inode_blocks(struct inode *inode);

// vvsfs_file_get_block
// @inode: inode of the file
// @iblock: the block number (relative to the beginning of the file) to be read.
// This is not
//          the actual disk block, but rather a logical block within a file.
// @bh: the buffer to load the disk block to
// @create: allocate a block on disk if it's not already allocated.
//
// This function translates a read operation for the "iblock"-th block
// in a file to the actual read operation on disk. It is used by the
// readpage/writepage operations for pagecache. This allows a simpler and more
// modular implementation of file read/write. All we need to do is to provide a
// primitive for reading one block at a time.
//
static int vvsfs_file_get_block(struct inode *inode,
                                sector_t iblock,
                                struct buffer_head *bh,
                                int create) {
    struct super_block *sb = inode->i_sb;
    struct vvsfs_inode_info *vi = VVSFS_I(inode);
    int raw_dno;
    uint32_t dno, bno;
    LOG("vvsfs - file_get_block");
    if (iblock >= VVSFS_MAX_INODE_BLOCKS) {
        DEBUG_LOG("vvsfs - file_get_block - block index exceeds maximum "
                  "supported: %u >= %d\n",
                  (uint32_t)iblock,
                  (int)VVSFS_MAX_INODE_BLOCKS);
        return -EFBIG;
    }
    if (iblock > vi->i_db_count) {
        return 0;
    }
    if (iblock == vi->i_db_count) {
        if (!create) {
            return 0;
        }
        raw_dno = vvsfs_assign_data_block(vi, sb, (uint32_t)iblock);
        if (raw_dno < 0) {
            DEBUG_LOG("vvsfs - file_get_block - failed to assign data block\n");
            return raw_dno;
        }
        mark_inode_dirty(inode);
        dno = (uint32_t)raw_dno;
        inode->i_blocks = vi->i_db_count * VVSFS_BLOCKSIZE / VVSFS_SECTORSIZE;
        bno = vvsfs_get_data_block(dno);
    } else {
        bno = vvsfs_get_data_block(
            vvsfs_index_data_block(vi, sb, (uint32_t)iblock));
    }
    map_bh(bh, sb, bno);
    LOG("vvsfs - file_get_block - done\n");
    return 0;
}

/* Determine if the dentry contains only reserved
 * entries
 *
 * @bh: Data block buffer
 * @dentry_count: Number of dentries in this block
 *
 * @return (int): 0 if there is a non-reserved entry
 * in any dentry, 1 otherwise
 */
static int vvsfs_dir_only_reserved(struct buffer_head *bh,
                                   struct inode *dir,
                                   int dentry_count) {
    struct vvsfs_dir_entry *dentry;
    char *name;
    uint32_t inumber;
    int d;
    for (d = 0; d < dentry_count; d++) {
        // Access the current dentry
        dentry = READ_DENTRY(bh, d);
        name = dentry->name;
        inumber = dentry->inode_number;
        if (IS_NON_RESERVED_DENTRY(name, inumber, dir)) {
            // If the dentry is not '.' or '..' (given
            // that the second case matches the inode
            // to the parent), then this is not empty
            DEBUG_LOG("vvsfs - dir_only_reserved - "
                      "non-reserved entry: name: "
                      "%s inumber: %u\n",
                      name,
                      inumber);
            return 0;
        }
    }
    return 1;
}

/* Determine if a given directory is empty (has only
 * reserved entries)
 *
 * @dir: Target directory inode
 *
 * @return: (int) 1 if true, 0 otherwise
 */
static int vvsfs_empty_dir(struct inode *dir) {
    struct vvsfs_inode_info *vi;
    struct buffer_head *bh;
    int i;
    int last_block_dentry_count;
    int current_block_dentry_count;
    DEBUG_LOG("vvsfs - empty_dir\n");

    // Check that this is actually a directory
    if (!S_ISDIR(dir->i_mode)) {
        DEBUG_LOG("vvsfs - empty_dir - not actually a directory\n");
        return -ENOTDIR;
    }

    // Retrieve vvsfs specific inode data from dir inode
    vi = VVSFS_I(dir);
    LAST_BLOCK_DENTRY_COUNT(dir, last_block_dentry_count);
    // Retrieve superblock object for R/W to disk
    // blocks
    DEBUG_LOG("vvsfs - empty_dir - number of blocks "
              "to read %d\n",
              vi->i_db_count);
    // Progressively load datablocks into memory and
    // check dentries
    for (i = 0; i < vi->i_db_count; i++) {
        LOG("vvsfs - empty_dir - reading dno: %d, "
            "disk block: %d\n",
            vi->i_data[i],
            vvsfs_get_data_block(vi->i_data[i]));
        bh = READ_BLOCK(dir->i_sb, vi, i);
        if (!bh) {
            // Buffer read failed, no more data when
            // we expected some
            DEBUG_LOG("vvsfs - empty_dir - buffer "
                      "read failed\n");
            return -EIO;
        }
        current_block_dentry_count = i == vi->i_db_count - 1
                                         ? last_block_dentry_count
                                         : VVSFS_N_DENTRY_PER_BLOCK;
        // Check if there are any non-reserved
        // dentries
        if (!vvsfs_dir_only_reserved(bh, dir, current_block_dentry_count)) {
            brelse(bh);
            DEBUG_LOG("vvsfs - empty_dir - done (false)\n");
            return 0;
        }
        brelse(bh);
    }
    DEBUG_LOG("vvsfs - empty_dir - done (true)\n");
    return 1;
}

/**
 * Given a dentry, exchange which inode the dentry points to
 * @dir: the parent directory which contains the dentry
 * @dentry: target dentry to update the inode number of
 * @existing_inode: the inode which the dentry currently points to
 *      Note: the link count and modification time of this inode will be updated
 * @replacement_inode_no: the new inode number to store in the dentry
 *
 * It is the responsibility of the calling function to update the link count
 * of the replacement inode depending on the circumstance (e.g. mv, link)
 */
static int vvsfs_dentry_exchange_inode(struct inode *dir,
                                       struct dentry *dentry,
                                       struct inode *existing_inode,
                                       uint32_t replacement_inode_no) {
    int err;
    struct bufloc_t loc;

    // Get the vvsfs representation of the dentry
    err = vvsfs_find_entry(
        dir, dentry, BL_PERSIST_BUFFER | BL_PERSIST_DENTRY, &loc);
    if (err) {
        DEBUG_LOG("vvsfs - exchange_inode - failed to find new dentry\n");
        return -ENOENT;
    }

    // Update the dentry to point to the new inode
    loc.dentry->inode_number = replacement_inode_no;
    mark_buffer_dirty(loc.bh);
    // We sync this data to disk now. Although another dentry may also point to
    // this inode, that is better than potentially having a point in time where
    // this dentry is not pointing to any inode
    sync_dirty_buffer(loc.bh);
    brelse(loc.bh);

    // We have changed a dentry, so update the parent directory time stats
    dir->i_mtime = dir->i_ctime = current_time(dir);
    mark_inode_dirty(dir);

    // Drop the link count of the inode that was originally pointed to by this
    // dentry, so that it can possibly be deleted
    existing_inode->i_ctime = current_time(existing_inode);
    DEBUG_LOG("vvsfs - rename - new_inode link count before: %u\n",
              existing_inode->i_nlink);
    inode_dec_link_count(existing_inode);
    DEBUG_LOG("vvsfs - rename - new_inode link count after: %u\n",
              existing_inode->i_nlink);
    mark_inode_dirty(existing_inode);
    return 0;
}

// This function is needed to initiate the inode
// cache, to allow us to attach filesystem specific
// inode information. You don't need to modify this.
int vvsfs_init_inode_cache(void) {
    LOG("vvsfs - init inode cache ");

    vvsfs_inode_cache = kmem_cache_create(
        "vvsfs_cache", sizeof(struct vvsfs_inode_info), 0, 0, NULL);
    if (!vvsfs_inode_cache)
        return -ENOMEM;
    return 0;
}

// De-allocate the inode cache
void vvsfs_destroy_inode_cache(void) {
    LOG("vvsfs - destroy_inode_cache ");

    kmem_cache_destroy(vvsfs_inode_cache);
}

// vvsfs_iget - get the inode from the super block
// This function will either return the inode that
// corresponds to a given inode number (ino), if it's
// already in the cache, or create a new inode object,
// if it's not in the cache. Note that this is very
// similar to vvsfs_new_inode, except that the
// requested inode is supposed to be allocated on-disk
// already. So don't use this to create a completely
// new inode that has not been allocated on disk.
struct inode *vvsfs_iget(struct super_block *sb, unsigned long ino) {
    struct inode *inode;
    struct vvsfs_inode *disk_inode;
    struct vvsfs_inode_info *inode_info;
    struct buffer_head *bh;
    uint32_t inode_block;
    uint32_t inode_offset;
    int i;

    DEBUG_LOG("vvsfs - iget - ino : %d", (unsigned int)ino);
    DEBUG_LOG(" super %p\n", sb);

    inode = iget_locked(sb, ino);
    if (!inode)
        return ERR_PTR(-ENOMEM);
    if (!(inode->i_state & I_NEW))
        return inode;

    inode_info = VVSFS_I(inode);

    inode_block = vvsfs_get_inode_block(ino);
    inode_offset = vvsfs_get_inode_offset(ino);

    bh = sb_bread(sb, inode_block);
    if (!bh) {
        LOG("vvsfs - iget - failed sb_read");
        return ERR_PTR(-EIO);
    }

    disk_inode = (struct vvsfs_inode *)(bh->b_data + inode_offset);
    inode->i_mode = disk_inode->i_mode;
    i_uid_write(inode, disk_inode->i_uid);
    i_gid_write(inode, disk_inode->i_gid);
    inode->i_size = disk_inode->i_size;

    // set the access/modication/creation times
    inode->i_atime.tv_sec = disk_inode->i_atime;
    inode->i_mtime.tv_sec = disk_inode->i_mtime;
    inode->i_ctime.tv_sec = disk_inode->i_ctime;
    // minix sets the nsec's to 0
    inode->i_atime.tv_nsec = 0;
    inode->i_mtime.tv_nsec = 0;
    inode->i_ctime.tv_nsec = 0;

    // set the link count; note that we can't set
    // inode->i_nlink directly; we need to use the
    // set_nlink function here.
    set_nlink(inode, disk_inode->i_links_count);
    inode->i_blocks =
        disk_inode->i_data_blocks_count * (VVSFS_BLOCKSIZE / VVSFS_SECTORSIZE);

    inode_info->i_db_count = disk_inode->i_data_blocks_count;
    /* store data blocks in cache */
    for (i = 0; i < VVSFS_N_BLOCKS; ++i)
        inode_info->i_data[i] = disk_inode->i_block[i];

    if (S_ISREG(inode->i_mode)) {
        inode->i_op = &vvsfs_file_inode_operations;
        inode->i_fop = &vvsfs_file_operations;
        inode->i_mapping->a_ops = &vvsfs_as_operations;
    } else if (S_ISDIR(inode->i_mode)) {
        inode->i_op = &vvsfs_dir_inode_operations;
        inode->i_fop = &vvsfs_dir_operations;
    } else if (S_ISLNK(inode->i_mode)) {
        inode->i_op = &vvsfs_symlink_inode_operations;
        // since we are using page_symlink we need to set this first
        inode_nohighmem(inode);
        inode->i_mapping->a_ops = &vvsfs_as_operations;
    } else {
        // special file
        init_special_inode(inode, inode->i_mode, disk_inode->i_rdev);
    }

    brelse(bh);

    unlock_new_inode(inode);

    return inode;
}

// mounting the file system -- leave as is.
static struct dentry *vvsfs_mount(struct file_system_type *fs_type,
                                  int flags,
                                  const char *dev_name,
                                  void *data) {
    return mount_bdev(fs_type, flags, dev_name, data, vvsfs_fill_super);
}

static struct file_system_type vvsfs_type = {
    .owner = THIS_MODULE,
    .name = "vvsfs",
    .mount = vvsfs_mount,
    .kill_sb = kill_block_super,
    .fs_flags = FS_REQUIRES_DEV,
};

static int __init vvsfs_init(void) {
    int ret = vvsfs_init_inode_cache();
    if (ret) {
        LOG("inode cache creation failed");
        return ret;
    }

    LOG("Registering vvsfs\n");
    return register_filesystem(&vvsfs_type);
}

static void __exit vvsfs_exit(void) {
    LOG("Unregistering the vvsfs.\n");
    unregister_filesystem(&vvsfs_type);
    vvsfs_destroy_inode_cache();
}

module_init(vvsfs_init);
module_exit(vvsfs_exit);
MODULE_LICENSE("GPL");
