
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

#include "buffer_utils.h"
#include "logging.h"
#include "vvsfs.h"

// Address pace operation readpage/readfolio.
// You do not need to modify this.
static int
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
vvsfs_readpage(struct file *file, struct page *page) {
    LOG("vvsfs - readpage");
    return mpage_readpage(page, vvsfs_file_get_block);
}
#else
vvsfs_read_folio(struct file *file, struct folio *folio) {
    LOG("vvsfs - read folio");
    return mpage_read_folio(folio, vvsfs_file_get_block);
}
#endif

// Address pace operation readpage.
// You do not need to modify this.
static int vvsfs_writepage(struct page *page, struct writeback_control *wbc) {
    LOG("vvsfs - writepage");

    return block_write_full_page(page, vvsfs_file_get_block, wbc);
}

// Address pace operation readpage.
// You do not need to modify this.
static int vvsfs_write_begin(struct file *file,
                             struct address_space *mapping,
                             loff_t pos,
                             unsigned int len,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
                             unsigned int flags,
#endif
                             struct page **pagep,
                             void **fsdata) {
    LOG("vvsfs - write_begin [%lu]\n", mapping->host->i_ino);

    if (pos + len > VVSFS_MAXFILESIZE)
        return -EFBIG;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
    return block_write_begin(
        mapping, pos, len, flags, pagep, vvsfs_file_get_block);
#else
    return block_write_begin(mapping, pos, len, pagep, vvsfs_file_get_block);
#endif
}

// Address pace operation readpage.
// May require some modification to include additonal inode data.
static int vvsfs_write_end(struct file *file,
                           struct address_space *mapping,
                           loff_t pos,
                           unsigned int len,
                           unsigned int copied,
                           struct page *page,
                           void *fsdata) {
    struct inode *inode = mapping->host;
    struct vvsfs_inode_info *vi = VVSFS_I(inode);
    int ret;

    LOG("vvsfs - write_end, [%lu]\n", inode->i_ino);

    ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);
    if (ret < len) {
        LOG("wrote less than requested.");
        return ret;
    }

    /* Update inode metadata */
    inode->i_blocks = vi->i_db_count * VVSFS_BLOCKSIZE / VVSFS_SECTORSIZE;
    inode->i_mtime = inode->i_ctime = current_time(inode);
    mark_inode_dirty(inode);

    return ret;
}

static const struct address_space_operations vvsfs_as_operations = {
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 19, 0)
    .readpage = vvsfs_readpage,
#else
    .read_folio = vvsfs_read_folio,
#endif
    .writepage = vvsfs_writepage,
    .write_begin = vvsfs_write_begin,
    .write_end = vvsfs_write_end,
};
