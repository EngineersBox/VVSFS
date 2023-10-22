/*
 * The Very Very Simple File System (vvsfs)
 * Eric McCreath 2006, 2008, 2010, 2020, 2023 - GPL
 * (based on the simplistic RAM filesystem McCreath 2001)
 *
 * Alwen Tiu, 2023. Added various improvements to allow multiple data blocks,
 * bitmaps, and using address space operations to simplify read/write.
 */

#include "logging.h"
#include "vvsfs.h"

// inode cache -- this is used to attach vvsfs specific inode
// data to the vfs inode
struct kmem_cache *vvsfs_inode_cache;

struct inode *vvsfs_iget(struct super_block *sb, unsigned long ino);

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

struct file_system_type vvsfs_type = {
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
