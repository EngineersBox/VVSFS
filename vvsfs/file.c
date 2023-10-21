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

// File operations; leave as is. We are using the generic VFS implementations
// to take care of read/write/seek/fsync. The read/write operations rely on the
// address space operations, so there's no need to modify these.
const struct file_operations vvsfs_file_operations = {
    .llseek = generic_file_llseek,
    .fsync = generic_file_fsync,
    .read_iter = generic_file_read_iter,
    .write_iter = generic_file_write_iter,
};

const struct inode_operations vvsfs_file_inode_operations = {};