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
#include "logging.h"

// vvsfs_readdir - reads a directory and places the result using filldir, cached
// in dcache
static int vvsfs_readdir(struct file *filp, struct dir_context *ctx) {
    struct inode *dir;
    struct vvsfs_dir_entry *dentry;
    char *data;
    int i;
    int num_dirs;
    int err;
    DEBUG_LOG("vvsfs - readdir\n");
    // get the directory inode from file
    dir = file_inode(filp);
    data = vvsfs_read_dentries(dir, &num_dirs);
    if (IS_ERR(data)) {
        int err = PTR_ERR(data);
        DEBUG_LOG("vvsfs - readdir - failed cached dentries read: %d\n", err);
        return err;
    }
    // Iterate over dentries and emit them into the dcache
    for (i = ctx->pos / VVSFS_DENTRYSIZE;
         i < num_dirs && filp->f_pos < dir->i_size;
         ++i) {
        dentry = READ_DENTRY_OFF(data, i);
        if (!(err = dir_emit(ctx,
                             dentry->name,
                             strnlen(dentry->name, VVSFS_MAXNAME),
                             dentry->inode_number,
                             DT_UNKNOWN))) {
            DEBUG_LOG("vvsfs - readdir - failed dir_emit: %d\n", err);
            kfree(data);
            return -ENOENT;
        }
        ctx->pos += VVSFS_DENTRYSIZE;
    }
    kfree(data);
    DEBUG_LOG("vvsfs - readdir - done");
    return 0;
}

const struct file_operations vvsfs_dir_operations = {
    .llseek = generic_file_llseek,
    .read = generic_read_dir,
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 5, 0)
    .iterate = vvsfs_readdir,
#else
    .iterate_shared = vvsfs_readdir,
#endif
    .fsync = generic_file_fsync,
};
