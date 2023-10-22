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

#include "logging.h"
#include "vvsfs.h"

struct inode *vvsfs_iget(struct super_block *sb, unsigned long ino);

/* Find a dentry within a given block by name
 * (returned through parameter)
 *
 * @bh: Data block buffer
 * @dentry_count: Number of dentries in this block
 * @i: Data block index
 * @target_name: Name of the dentry to be found
 * @target_name_len: Length of target name
 * @flags: Behaviour flags for bufloc_t construction
 * @out_loc: Returned data for any potentially found
 * matching dentry
 *
 * @return: (int) 0 if found, 1 otherwise
 */
static int vvsfs_find_entry_in_block(struct buffer_head *bh,
                                     int dentry_count,
                                     int i,
                                     const char *target_name,
                                     int target_name_len,
                                     int flags,
                                     struct bufloc_t *out_loc) {
    struct vvsfs_dir_entry *dentry;
    char *name;
    uint32_t inumber;
    int d;
    for (d = 0; d < dentry_count; d++) {
        // Access the current dentry
        dentry = READ_DENTRY(bh, d);
        name = dentry->name;
        inumber = dentry->inode_number;
        //  Skip if reserved or name does not match
        DEBUG_LOG("vvsfs - find_entry_in_block - "
                  "comparing %s (%zu) == %s (%d)\n",
                  name,
                  strlen(name),
                  target_name,
                  target_name_len);
        if (!inumber || !namecmp(name, target_name, target_name_len) != 0) {
            continue;
        }
        out_loc->b_index = i;
        out_loc->d_index = d;
        out_loc->flags = flags;
        if (bl_flag_set(flags, BL_PERSIST_BUFFER)) {
            out_loc->bh = bh;
            out_loc->dentry =
                bl_flag_set(flags, BL_PERSIST_DENTRY) ? dentry : NULL;
        } else {
            out_loc->bh = NULL;
            out_loc->dentry = NULL;
            brelse(bh);
        }
        DEBUG_LOG("vvsfs - find_entry_in_block - "
                  "done (found)\n");
        return 0;
    }
    brelse(bh);
    DEBUG_LOG("vvsfs - find_entry_in_block - done "
              "(not found)\n");
    return 1;
}

/* Find a given entry within the given directory inode
 * direct blocks
 *
 * @vi: Inode info of directory to search
 * @sb: Superblock of filesystem
 * @dentry: Target dentry (name, length, etc)
 * @flags: Behavioural flags for bufloc_t data
 * @out_loc: Returned data for location of entry if
 * found
 *
 * @return: (int): 0 if found, 1 if not found,
 *                 otherwise an error
 */
static int vvsfs_find_entry_direct(struct vvsfs_inode_info *vi,
                                   struct super_block *sb,
                                   struct dentry *dentry,
                                   struct bufloc_t *out_loc,
                                   unsigned flags,
                                   int last_block_dentry_count) {
    struct buffer_head *bh;
    const char *target_name;
    int i;
    int direct_blocks;
    int current_block_dentry_count;
    int target_name_len;
    DEBUG_LOG("vvsfs - find_entry - direct blocks\n");
    target_name = dentry->d_name.name;
    target_name_len = dentry->d_name.len;
    direct_blocks =
        min(vi->i_db_count, (uint32_t)VVSFS_LAST_DIRECT_BLOCK_INDEX);
    for (i = 0; i < direct_blocks; i++) {
        LOG("vvsfs - find_entry - reading dno: "
            "%d, disk block: %d",
            vi->i_data[i],
            vvsfs_get_data_block(vi->i_data[i]));
        bh = READ_BLOCK(sb, vi, i);
        if (!bh) {
            // Buffer read failed, no more data when
            // we expected some
            return -EIO;
        }
        current_block_dentry_count = i == vi->i_db_count - 1
                                         ? last_block_dentry_count
                                         : VVSFS_N_DENTRY_PER_BLOCK;
        if (!vvsfs_find_entry_in_block(bh,
                                       current_block_dentry_count,
                                       i,
                                       target_name,
                                       target_name_len,
                                       flags,
                                       out_loc)) {
            DEBUG_LOG("vvsfs - find_entry - direct done (found)");
            return 0;
        }
        // buffer_head release is handled within block
        // search
    }
    return 1;
}

/* Find a given entry within the given directory inode
 * indirect blocks
 *
 * @vi: Inode info of directory to search
 * @sb: Superblock of filesystem
 * @dentry: Target dentry (name, length, etc)
 * @flags: Behavioural flags for bufloc_t data
 * @out_loc: Returned data for location of entry if
 * found
 *
 * @return: (int): 0 if found, 1 if not found,
 *                 otherwise an error
 */
static int vvsfs_find_entry_indirect(struct vvsfs_inode_info *vi,
                                     struct super_block *sb,
                                     struct dentry *dentry,
                                     struct bufloc_t *out_loc,
                                     unsigned flags,
                                     int last_block_dentry_count) {
    struct buffer_head *i_bh;
    struct buffer_head *bh;
    const char *target_name;
    int indirect_blocks;
    int current_block_dentry_count;
    int i;
    int target_name_len;
    DEBUG_LOG("vvsfs - find_entry - indirect blocks\n");
    target_name = dentry->d_name.name;
    target_name_len = dentry->d_name.len;
    i_bh = READ_BLOCK(sb, vi, VVSFS_LAST_DIRECT_BLOCK_INDEX);
    if (!i_bh) {
        return -EIO;
    }
    indirect_blocks = vi->i_db_count - VVSFS_LAST_DIRECT_BLOCK_INDEX;
    for (i = 0; i < indirect_blocks; i++) {
        bh = READ_INDIRECT_BLOCK(sb, i_bh, i);
        if (!bh) {
            brelse(i_bh);
            return -EIO;
        }
        current_block_dentry_count = i == vi->i_db_count - 1
                                         ? last_block_dentry_count
                                         : VVSFS_N_DENTRY_PER_BLOCK;
        if (!vvsfs_find_entry_in_block(bh,
                                       current_block_dentry_count,
                                       i,
                                       target_name,
                                       target_name_len,
                                       flags,
                                       out_loc)) {
            DEBUG_LOG("vvsfs - find_entry - indirect done (found)");
            out_loc->b_index += VVSFS_LAST_DIRECT_BLOCK_INDEX;
            brelse(i_bh);
            return 0;
        }
        // buffer_head release is handled within block
        // search
    }
    brelse(i_bh);
    return 1;
}

/* Find a given entry within the given directory inode
 *
 * @dir: Inode representation of directory to search
 * @dentry: Target dentry (name, length, etc)
 * @flags: Behavioural flags for bufloc_t data
 * @out_loc: Returned data for location of entry if
 * found
 *
 * @return: (int): 0 if found, 1 if not found,
 * otherwise and error
 */
int vvsfs_find_entry(struct inode *dir,
                     struct dentry *dentry,
                     unsigned flags,
                     struct bufloc_t *out_loc) {
    struct vvsfs_inode_info *vi;
    struct super_block *sb;
    int result;
    int last_block_dentry_count;
    DEBUG_LOG("vvsfs - find_entry\n");
    // Retrieve vvsfs specific inode data from dir
    // inode
    vi = VVSFS_I(dir);
    sb = dir->i_sb;
    LAST_BLOCK_DENTRY_COUNT(dir, last_block_dentry_count);
    DEBUG_LOG("vvsfs - find_entry - number of blocks "
              "to read %d\n",
              vi->i_db_count);
    // Progressively load datablocks into memory and
    // check dentries
    result = vvsfs_find_entry_direct(
        vi, sb, dentry, out_loc, flags, last_block_dentry_count);
    if (result <= 0) {
        return result;
    }
    result = vvsfs_find_entry_indirect(
        vi, sb, dentry, out_loc, flags, last_block_dentry_count);
    if (result <= 0) {
        return result;
    }
    DEBUG_LOG("vvsfs - find_entry - done (not found)");
    return 1;
}

/* Shift back blocks and fill holes for direct blocks only.
 * Assumed that the target block is within the direct blocks.
 *
 * @vi: Inode information for inode of target blocks and dentries
 * @i_sb: VVSFS specific superblock information
 * @bh: Buffer for the indirect block in the last entry of direct
 *      blocks in this inode
 * @block_indexx: Target freed data block index
 */
static void vvsfs_shift_direct_only(struct vvsfs_inode_info *vi,
                                    uint32_t block_index) {
    size_t count;
    DEBUG_LOG(
        "vvsfs - shift_blocks_back - only direct, index: %u, blocks: %u\n",
        block_index,
        vi->i_db_count);
    count = (--vi->i_db_count) - block_index;
    DEBUG_LOG("vvsfs - shift_blocks_back - count: %zu\n", count);
    DEBUG_LOG("vvsfs - shift_blocks_back - "
              "i_db_count after: %d\n",
              vi->i_db_count);
    if (count > 0) {
        // Implementation specific behaviour for memmove len of 0
        // just don't do it and avoid poptential issues
        memmove(&vi->i_data[block_index],
                &vi->i_data[block_index + 1],
                count * VVSFS_INDIRECT_PTR_SIZE);
    }
    // Ensure the last block is not set (avoids
    // duplication of last element from shift back)
    vi->i_data[vi->i_db_count] = 0;
}

/* Shift back blocks and fill holes for indirect blocks only.
 * Assumed that the target block is within the indirect blocks.
 *
 * @vi: Inode information for inode of target blocks and dentries
 * @i_sb: VVSFS specific superblock information
 * @bh: Buffer for the indirect block in the last entry of direct
 *      blocks in this inode
 * @block_indexx: Target freed data block index
 */
static void vvsfs_shift_indirect_only(struct vvsfs_inode_info *vi,
                                      struct vvsfs_sb_info *i_sb,
                                      struct buffer_head *bh,
                                      uint32_t block_index) {
    size_t count;
    DEBUG_LOG("vvsfs - shift_blocks_back - only indirect, index: %u, "
              "blocks: %u\n",
              block_index,
              vi->i_db_count);
    count = (--vi->i_db_count) - block_index;
    DEBUG_LOG("vvsfs - shift_blocks_back - "
              "i_db_count after: %d\n",
              vi->i_db_count);
    if (count > 0) {
        memmove(bh->b_data + (block_index * VVSFS_INDIRECT_PTR_SIZE),
                bh->b_data + ((block_index + 1) * VVSFS_INDIRECT_PTR_SIZE),
                count * VVSFS_INDIRECT_PTR_SIZE);
    }
    if (vi->i_db_count == VVSFS_N_BLOCKS) {
        DEBUG_LOG("vvsfs - shift_blocks_back - was last indrect, freeing "
                  "indirect block\n");
        vvsfs_free_data_block(i_sb->dmap,
                              vi->i_data[VVSFS_LAST_DIRECT_BLOCK_INDEX]);
    } else {
        DEBUG_LOG("vvsfs - shift_blocks_back - was not last indirect, "
                  "setting last index %d to zero\n",
                  vi->i_db_count - VVSFS_LAST_DIRECT_BLOCK_INDEX);
        // Ensure the last block is not set (avoids
        // duplication of last element from shift back)
        write_int_to_buffer(
            bh->b_data + ((vi->i_db_count - VVSFS_LAST_DIRECT_BLOCK_INDEX) *
                          VVSFS_INDIRECT_PTR_SIZE),
            0);
    }
    mark_buffer_dirty(bh);
    brelse(bh);
}

/* Shifts data blocks around to ensure that there are not holes,
 * this method assumes that a data block has been freed, which
 * is the target to fill. Full generality across direct and
 * indirect blocks is supported (all support conditional
 * deallocation of the last block if newly emptied to fill
 * earlierblock dentry hole):
 * - Direct only movement (no indirect blocks in use)
 * - Indirect block dentry moved to direct block hole
 * - Indirect block dentry moved to other indirect block hole
 *
 * Note that this method will mark the buffer dirt and release
 * it, persiting any changes made.
 *
 * @vi: Inode information for inode of target blocks and dentries
 * @sb: Superblock of the filesystem
 * @i_sb: VVSFS specific superblock information
 * @block_index: Target freed data block index
 *
 * @return: (int) 0 if successful, error otherwise
 */
static int vvsfs_shift_blocks_back(struct vvsfs_inode_info *vi,
                                   struct super_block *sb,
                                   struct vvsfs_sb_info *i_sb,
                                   uint32_t block_index) {
    struct buffer_head *bh;
    uint32_t replacement;
    size_t count;
    if (vi->i_db_count < VVSFS_N_BLOCKS) {
        // Only direct blocks
        vvsfs_shift_direct_only(vi, block_index);
        return 0;
    }
    bh = READ_BLOCK(sb, vi, VVSFS_LAST_DIRECT_BLOCK_INDEX);
    if (!bh) {
        return -EIO;
    }
    if (block_index >= VVSFS_N_BLOCKS) {
        // Only indirect blocks
        vvsfs_shift_indirect_only(vi, i_sb, bh, block_index);
        return 0;
    }
    replacement = read_int_from_buffer(bh->b_data);
    vi->i_db_count--;
    count = VVSFS_N_BLOCKS - block_index;
    if (block_index < VVSFS_LAST_DIRECT_BLOCK_INDEX) {
        // Move indirect block to direct
        DEBUG_LOG("vvsfs - shift_blocks_back - has indirect, is direct, index: "
                  "%u, blocks: %u\n",
                  block_index,
                  vi->i_db_count + 1);
        memmove(&vi->i_data[block_index],
                &vi->i_data[block_index + 1],
                count * sizeof(uint32_t));
        DEBUG_LOG("vvsfs - shift_blocks_back - was not lat indect, setting "
                  "last index %d to zero\n",
                  vi->i_db_count - VVSFS_N_BLOCKS);
        // Overwrite the old last block pointer with zeros
        write_int_to_buffer(
            bh->b_data + ((vi->i_db_count - VVSFS_LAST_DIRECT_BLOCK_INDEX) *
                          VVSFS_INDIRECT_PTR_SIZE),
            0);
        // Actually assign the indirect block to the end of the direct blocks
        // this also avoids needing to write zeros to the end
        vi->i_data[VVSFS_LAST_DIRECT_BLOCK_INDEX - 2] = replacement;
        DEBUG_LOG("vvsfs - shift_blocks_back - writing first entry %u from "
                  "indirect blocks to last direct block\n",
                  replacement);
    }
    if (vi->i_db_count == VVSFS_LAST_DIRECT_BLOCK_INDEX) {
        // Moved indirect block was last, since it is now a direct block
        // we can free the indirect block in the last index of vi->i_data
        DEBUG_LOG("vvsfs - shift_blocks_back - shifted last indirect block, "
                  "freeing indirect block\n");
        brelse(bh);
        vvsfs_free_data_block(i_sb->dmap,
                              vi->i_data[VVSFS_LAST_DIRECT_BLOCK_INDEX]);
        vi->i_data[VVSFS_LAST_DIRECT_BLOCK_INDEX] = 0;
        DEBUG_LOG("vvsfs - shift_blocks_back - zeroed last direct block "
                  "(indirect pointer)\n");
        return 0;
    }
    count = block_index - VVSFS_LAST_DIRECT_BLOCK_INDEX;
    if (count > 0) {
        // Shift all indirect blocks after hole down
        memmove(bh->b_data,
                bh->b_data + VVSFS_INDIRECT_PTR_SIZE,
                count * VVSFS_INDIRECT_PTR_SIZE);
    }
    DEBUG_LOG("vvsfs - shift_blocks_back - was not last indirect, setting last "
              "index %d to zero\n",
              vi->i_db_count - VVSFS_N_BLOCKS);
    // Overwrite the old last block pointer with zeros
    write_int_to_buffer(bh->b_data +
                            ((vi->i_db_count - VVSFS_LAST_DIRECT_BLOCK_INDEX) *
                             VVSFS_INDIRECT_PTR_SIZE),
                        0);
    mark_buffer_dirty(bh);
    brelse(bh);
    return 0;
}

/* Deallocate a data block from the given inode
 *
 * @inode: Target inode to deallocate data block from
 * @block_index: index into inode data blocks, can be
 *               direct or indirect
 *
 * @return: (int) 0 if successful, error otherwise
 */
static int vvsfs_dealloc_data_block(struct inode *inode, int block_index) {
    struct vvsfs_inode_info *vi;
    struct super_block *sb;
    struct vvsfs_sb_info *sb_info;
    int err;
    int raw_db_index;
    uint32_t db_index;
    DEBUG_LOG("vvsfs - dealloc_data_block\n");
    if (block_index < 0 || block_index >= VVSFS_MAX_INODE_BLOCKS) {
        DEBUG_LOG("vvsfs - dealloc_data_block - "
                  "block_index (%d) out of range "
                  "%d-%d\n",
                  block_index,
                  0,
                  (int)VVSFS_MAX_INODE_BLOCKS - 1);
        return -EINVAL;
    }
    vi = VVSFS_I(inode);
    sb = inode->i_sb;
    sb_info = sb->s_fs_info;
    DEBUG_LOG("vvsfs - dealloc_data_block - target block: %d\n", block_index);
    raw_db_index = vvsfs_index_data_block(vi, sb, (uint32_t)block_index);
    if (raw_db_index < 0) {
        DEBUG_LOG("vvsfs - dealloc_data_block - indexing block %d failed\n",
                  block_index);
        return raw_db_index;
    }
    db_index = (uint32_t)raw_db_index;
    DEBUG_LOG("vvsfs - dealloc_data_block - removing "
              "block %d @ %u\n",
              block_index,
              db_index);

    vvsfs_free_data_block(sb_info->dmap, db_index);
    // Move all subsequent blocks back to fill the
    // holes
    if ((err = vvsfs_shift_blocks_back(vi, sb, sb_info, block_index))) {
        DEBUG_LOG("vvsfs - dealloc_data_block - unable to shift blocks, block "
                  "has been freed\n");
        return err;
    }
    mark_inode_dirty(inode);
    DEBUG_LOG("vvsfs - dealloc_data_block - done\n");
    return 0;
}

/* Remove the dentry specified via bufloc from the
 * last data block
 *
 * @dir: Target directory inode
 * @bufloc: Specification of dentry location in data
 * block (assumes already resolved)
 *
 * @return: (int) 0 if successful, error otherwise
 */
static int vvsfs_delete_entry_last_block(struct inode *dir,
                                         struct bufloc_t *bufloc) {
    struct vvsfs_dir_entry *last_dentry;
    int last_block_dentry_count;
    int err;
    DEBUG_LOG("vvsfs - delete_entry_last_block\n");
    LAST_BLOCK_DENTRY_COUNT(dir, last_block_dentry_count);
    if (bufloc->d_index == last_block_dentry_count - 1) {
        // Last dentry in block remove cleanly
        DEBUG_LOG("vvsfs - delete_entry_bufloc - "
                  "last block, last dentry "
                  "in block, zero the entry\n");
        memset(bufloc->dentry, 0, VVSFS_DENTRYSIZE);
        DEBUG_LOG("vvsfs - delete_entry_bufloc - last count: %d, b_index: %u\n",
                  last_block_dentry_count,
                  bufloc->b_index);
        if (last_block_dentry_count == 1 &&
            (err = vvsfs_dealloc_data_block(dir, bufloc->b_index))) {
            return err;
        }
    } else {
        // Move last dentry in block to hole
        DEBUG_LOG("vvsfs - delete_entry_bufloc - "
                  "last block, not last "
                  "dentry in block, move last entry "
                  "to hole\n");
        last_dentry = READ_DENTRY(bufloc->bh, last_block_dentry_count - 1);
        memcpy(bufloc->dentry, last_dentry, VVSFS_DENTRYSIZE);
        // Delete the last dentry (as it has been
        // moved)
        memset(last_dentry, 0, VVSFS_DENTRYSIZE);
    }
    DEBUG_LOG("vvsfs - delete_entry_last_block - done\n");
    return 0;
}

/* Remove the dentry specified via bufloc from the
 * current data block
 *
 * @dir: Target directory inode
 * @bufloc: Specification of dentry location in data
 *          block (assumed already resolved)
 *
 * @return: (int) 0 if successful, error otherwise
 */
static int vvsfs_delete_entry_block(struct inode *dir,
                                    struct vvsfs_inode_info *vi,
                                    struct bufloc_t *bufloc) {
    struct vvsfs_dir_entry *last_dentry;
    struct buffer_head *i_bh;
    struct buffer_head *bh;
    struct super_block *sb;
    int last_block_dentry_count;
    int err;
    uint32_t index;
    DEBUG_LOG("vvsfs - delete_entry_block\n");
    LAST_BLOCK_DENTRY_COUNT(dir, last_block_dentry_count);
    DEBUG_LOG("vvsfs - delete_entry_block - last block dentry count: %d\n",
              last_block_dentry_count);
    sb = dir->i_sb;
    // Fill the hole with the last dentry in the last
    // block
    DEBUG_LOG("vvsfs - delete_entry_bufloc - not "
              "last block, fill hole "
              "from last block\n");
    if (vi->i_db_count < VVSFS_N_BLOCKS) {
        DEBUG_LOG("vvsfs - get_remove_last_dentry - direct block: %u\n",
                  vi->i_db_count - 1);
        bh = READ_BLOCK(sb, vi, vi->i_db_count - 1);
        if (!bh) {
            DEBUG_LOG("vvsfs - get_remove_last_dentry - failed to read direct "
                      "block\n");
            return -EIO;
        }
    } else {
        i_bh = READ_BLOCK(sb, vi, VVSFS_LAST_DIRECT_BLOCK_INDEX);
        if (!i_bh) {
            DEBUG_LOG("vvsfs - get_remove_last_dentry - failed to read "
                      "indirect block\n");
            return -EIO;
        }
        DEBUG_LOG("vvsfs - get_remove_last_dentry - indirect block: %u\n",
                  vi->i_db_count - VVSFS_N_BLOCKS);
        index = read_int_from_buffer(
            i_bh->b_data +
            ((vi->i_db_count - VVSFS_N_BLOCKS) * VVSFS_INDIRECT_PTR_SIZE));
        bh = READ_BLOCK_OFF(sb, index);
        if (!bh) {
            brelse(i_bh);
            return -EIO;
        }
        brelse(i_bh);
    }
    last_dentry = READ_DENTRY(bh, last_block_dentry_count - 1);
    memcpy(bufloc->dentry, last_dentry, VVSFS_DENTRYSIZE);
    // Delete the last dentry (as it has been moved)
    memset(last_dentry, 0, VVSFS_DENTRYSIZE);
    // Deallocate last block since we move the only dentry in it
    if (last_block_dentry_count == 1 &&
        (err = vvsfs_dealloc_data_block(dir, vi->i_db_count - 1))) {
        return err;
    }
    mark_buffer_dirty(bh);
    brelse(bh);
    DEBUG_LOG("vvsfs - delete_entry_block - done \n");
    return 0;
}

/* Delete and entry in a directory based on data
 * obtained through invocation of
 * `vvsfs_find_entry(...)`
 *
 * @dir: Inode representation of directory to search
 * @loc: Location of entry in data blocks
 *
 * @return: (int) 0 if successful, error otherwise
 */
static int vvsfs_delete_entry_bufloc(struct inode *dir,
                                     struct bufloc_t *bufloc) {
    struct vvsfs_inode_info *vi;
    int err;
    DEBUG_LOG("vvsfs - delete_entry_bufloc\n");
    vi = VVSFS_I(dir);
    // Resolve the buffer head and dentry if not
    // already done (indiciated by flags)
    if ((err = vvsfs_resolve_bufloc(dir, vi, bufloc))) {
        DEBUG_LOG("vvsfs - delete_entry_bufloc - "
                  "failed to resolve bufloc\n");
        return err;
    }
    DEBUG_LOG("vvsfs - delete_dentry_bufloc - block index: %u block count "
              "(index): %u\n",
              bufloc->b_index,
              vi->i_db_count - 1);
    // Determine if we are in the last block
    if (bufloc->b_index == vi->i_db_count - 1) {
        if ((err = vvsfs_delete_entry_last_block(dir, bufloc))) {
            DEBUG_LOG("vvsfs - delete_entry_bufloc - "
                      "failed to delete entry in "
                      "last block\n");
            return err;
        }
    } else {
        if ((err = vvsfs_delete_entry_block(dir, vi, bufloc))) {
            DEBUG_LOG("vvsfs - delete_entry_bufloc - "
                      "failed to delete entry in "
                      "block\n");
            return err;
        }
    }
    // Updated parent inode size and times
    dir->i_size -= VVSFS_DENTRYSIZE;
    dir->i_ctime = dir->i_mtime = current_time(dir);
    mark_buffer_dirty(bufloc->bh);
    brelse(bufloc->bh);
    mark_inode_dirty(dir);
    DEBUG_LOG("vvsfs - delete_entry_bufloc - done\n");
    return err;
}

/* Free all data blocks in a given inode
 *
 * @inode: Target inode to free all indirect and direct
 *         data blocks
 *
 * @return: (int) 0 if successful, error otherwise
 */
int vvsfs_free_inode_blocks(struct inode *inode) {
    struct super_block *sb;
    struct vvsfs_sb_info *i_sb;
    struct vvsfs_inode_info *vi;
    struct buffer_head *bh;
    int i;
    int indirect;
    int direct;
    uint32_t index;

    DEBUG_LOG("vvsfs - free inode blocks - %lu", inode->i_ino);

    if (inode->i_nlink != 0) {
        DEBUG_LOG(
            "vvsfs - free inode blocks called on allocated inode (links %u)",
            inode->i_nlink);
        return -EIO;
    }

    vi = VVSFS_I(inode);
    sb = inode->i_sb;
    i_sb = sb->s_fs_info;
    direct = min((int)VVSFS_LAST_DIRECT_BLOCK_INDEX, (int)vi->i_db_count);
    indirect =
        max((int)0, ((int)vi->i_db_count) - VVSFS_LAST_DIRECT_BLOCK_INDEX);
    for (i = 0; i < direct; i++) {
        vvsfs_free_data_block(i_sb->dmap, vi->i_data[i]);
    }
    if (indirect == 0) {
        goto free_inode;
    }
    bh = READ_BLOCK(sb, vi, VVSFS_LAST_DIRECT_BLOCK_INDEX);
    if (!bh) {
        return -EIO;
    }
    for (i = 0; i < indirect; i++) {
        index =
            read_int_from_buffer(bh->b_data + (i * VVSFS_INDIRECT_PTR_SIZE));
        vvsfs_free_data_block(i_sb->dmap, index);
    }
    brelse(bh);
    vvsfs_free_data_block(i_sb->dmap,
                          vi->i_data[VVSFS_LAST_DIRECT_BLOCK_INDEX]);
free_inode:
    vvsfs_free_inode_block(i_sb->imap, inode->i_ino);
    return 0;
}

/* Drops the link count of a given inode and frees the resources if applicable
 *
 * @inode: Target inode to drop link count of
 *
 * @return: (int) 0 if successful, error otherwise
 */
int vvsfs_drop_inode_link(struct inode *inode) {
    DEBUG_LOG("vvsfs - drop inode link");
    inode_dec_link_count(inode);
    if (inode->i_nlink == 0) {
        return vvsfs_free_inode_blocks(inode);
    }
    return 0;
}

/* Calculate the data block map index for a given position
 * within the given inode data blocks.
 *
 * @vi: Inode information of the target inode
 * @sb: Superblock of the filesytsem
 * @d_pos: Position of the data block within the inode
 *
 * @return: (int): 0 or greater if data block exists in
 *                 inode, error otherwise
 */
int vvsfs_index_data_block(struct vvsfs_inode_info *vi,
                           struct super_block *sb,
                           uint32_t d_pos) {
    struct buffer_head *bh;
    int offset;
    uint32_t index;
    DEBUG_LOG("vvsfs - index_data_block\n");
    DEBUG_LOG("vvsfs - index_data_block - d_pos: %u\n", d_pos);
    if (d_pos < VVSFS_LAST_DIRECT_BLOCK_INDEX) {
        DEBUG_LOG("vvsfs - index_data_block - direct done\n");
        return vi->i_data[d_pos];
    }
    bh = READ_BLOCK(sb, vi, VVSFS_LAST_DIRECT_BLOCK_INDEX);
    if (!bh) {
        DEBUG_LOG("vvsfs - index_data_block - failed to read buffer data\n");
        return -EIO;
    }
    offset = (d_pos - VVSFS_LAST_DIRECT_BLOCK_INDEX) * VVSFS_INDIRECT_PTR_SIZE;
    DEBUG_LOG("vvsfs - index_data_block - offset: %u\n", offset);
    index = read_int_from_buffer(bh->b_data + offset);
    brelse(bh);
    DEBUG_LOG(
        "vvsfs - index_data_block - indirect done: %u -> %d\n", offset, index);
    return index;
}

/* Given a position into the target inode data blocks,
 * create and assign a new data block.
 *
 * @dir_info: Inode information of target inode
 * @sb: Superblock of the filesystem
 * @d_pos: Data block position to create at
 *
 * @return: (int) 0 or greater, data block map index,
 *                error otherwise
 */
int vvsfs_assign_data_block(struct vvsfs_inode_info *dir_info,
                            struct super_block *sb,
                            uint32_t d_pos) {
    struct buffer_head *bh;
    struct vvsfs_sb_info *sbi = sb->s_fs_info;
    int offset;
    uint32_t indirect_block;
    uint32_t newblock;
    DEBUG_LOG("vvsfs - assign_data_block\n");
    newblock = vvsfs_reserve_data_block(sbi->dmap);
    if (!newblock) {
        return -ENOSPC;
    }
    DEBUG_LOG("vvsfs - assign-data_block - current block count: %u\n",
              dir_info->i_db_count);
    if (d_pos < VVSFS_LAST_DIRECT_BLOCK_INDEX) {
        DEBUG_LOG("vvsfs - assign_data_block - direct blocks free, allocating "
                  "direct\n");
        // Still have room in direct blocks, assign
        // there
        dir_info->i_data[d_pos] = newblock;
        dir_info->i_db_count++;
        goto done;
    }
    DEBUG_LOG("vvsfs - assign_data_block - no direct blocks free, allocating "
              "indirect\n");
    if (unlikely(dir_info->i_db_count < VVSFS_N_BLOCKS)) {
        // We don't have a block to store indirect pointers
        // use the newblock just allocated for consistency
        // and reserve another block for the actual data
        DEBUG_LOG("vvsfs - assign_data_block - indirect block not allocated, "
                  "allocating\n");
        indirect_block = newblock;
        newblock = vvsfs_reserve_data_block(sbi->dmap);
        if (!newblock) {
            vvsfs_free_data_block(sbi->dmap, indirect_block);
            return -ENOSPC;
        }
        dir_info->i_data[VVSFS_LAST_DIRECT_BLOCK_INDEX] = indirect_block;
    }
    bh = READ_BLOCK(sb, dir_info, VVSFS_LAST_DIRECT_BLOCK_INDEX);
    if (!bh) {
        DEBUG_LOG("vvsfs - assign_data_block - buffer read failed\n");
        vvsfs_free_data_block(sbi->dmap, newblock);
        vvsfs_free_data_block(sbi->dmap, indirect_block);
        dir_info->i_data[VVSFS_LAST_DIRECT_BLOCK_INDEX] = 0;
        return -EIO;
    }
    dir_info->i_db_count++;
    offset = (d_pos - VVSFS_LAST_DIRECT_BLOCK_INDEX) * VVSFS_INDIRECT_PTR_SIZE;
    DEBUG_LOG("vvsfs - assign_data_block - indirect block offset: %d <- %u\n",
              offset,
              newblock);

    write_int_to_buffer(bh->b_data + offset, newblock);
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
done:
    DEBUG_LOG("vvsfs - assign_data_block - done\n");
    return newblock;
}

// vvsfs_reserve_inode_block
// @map: a bitmap representing inode blocks on disk
//
// On success, this function returns a valid inode number and update the
// corresponding inode bitmap. On failure, it will return 0 (which is an invalid
// inode number), and leave the bitmap unchanged.
uint32_t vvsfs_reserve_inode_block(uint8_t *map) {
    uint32_t i = vvsfs_find_free_block(map, VVSFS_IMAP_SIZE);
    if (i == 0)
        return 0;
    return BNO_TO_INO(i);
}

// vvsfs_new_inode - find and construct a new inode.
// @dir: the inode of the parent directory where the new inode is supposed to be
// attached to.
// @mode: the mode information of the new inode
//
// This is a helper function for the inode operation "create" (implemented in
// vvsfs_create() ). It takes care of reserving an inode block on disk (by
// modifiying the inode bitmap), creating an VFS inode object (in memory) and
// attach filesystem-specific information to that VFS inode.
struct inode *
vvsfs_new_inode(const struct inode *dir, umode_t mode, dev_t rdev) {
    struct vvsfs_inode_info *inode_info;
    struct super_block *sb;
    struct vvsfs_sb_info *sbi;
    struct inode *inode;
    unsigned long dno, ino;
    int i;

    LOG("vvsfs - new inode\n");

    // get the filesystem specific info for the super block. The sbi object
    // contains the inode bitmap.
    sb = dir->i_sb;
    sbi = sb->s_fs_info;

    /*
        Find a spare inode in the vvsfs.
        The vvsfs_reserve_inode_block() will attempt to find the first free
       inode and allocates it, and returns the inode number. Note that the inode
       number is *not* the same as the disk block address on disk.
    */
    ino = vvsfs_reserve_inode_block(sbi->imap);
    if (BAD_INO(ino))
        return ERR_PTR(-ENOSPC);

    /*
        Find a spare data block.
        By default, a new data block is reserved for the new inode.
        This is probably a bit wasteful if the file/directory does not need it
        immediately.
        The `dno` here represents a data block position within the data bitmap
        so it's not the actual disk block location.
     */
    dno = vvsfs_reserve_data_block(sbi->dmap);
    if (dno == 0) {
        vvsfs_free_inode_block(
            sbi->imap,
            ino); // if we failed to allocate data block, release
                  // the inode block. and return an error code.
        return ERR_PTR(-ENOSPC);
    }

    /* create a new VFS (in memory) inode */
    inode = new_inode(sb);
    if (!inode) {
        // if failed, release the inode/data blocks so they can be reused.
        vvsfs_free_inode_block(sbi->imap, ino);
        vvsfs_free_data_block(sbi->dmap, dno);
        return ERR_PTR(-ENOMEM);
    }

    // fill in various information for the VFS inode.
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
    inode_init_owner(&init_user_ns, inode, dir, mode);
#else
    inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
#endif
    inode->i_ino = ino;
    inode->i_ctime = inode->i_mtime = inode->i_atime = current_time(inode);
    inode->i_mode = mode;
    inode->i_size = 0;
    inode->i_blocks = (VVSFS_BLOCKSIZE / VVSFS_SECTORSIZE);
    // increment the link counter. This basically increments inode->i_nlink,
    // but that member cannot be modified directly. Use instead set_nlink to set
    // it to a specific value.
    set_nlink(inode, 1);

    // check if the inode is for a directory, using the macro S_ISDIR
    if (S_ISREG(mode)) {
        inode->i_op = &vvsfs_file_inode_operations;
        inode->i_fop = &vvsfs_file_operations;
        inode->i_mapping->a_ops = &vvsfs_as_operations;
    } else if (S_ISDIR(mode)) {
        inode->i_op = &vvsfs_dir_inode_operations;
        inode->i_fop = &vvsfs_dir_operations;
    } else if (S_ISLNK(mode)) {
        inode->i_op = &vvsfs_symlink_inode_operations;
        // since we are using page_symlink we need to set this first
        inode_nohighmem(inode);
        inode->i_mapping->a_ops = &vvsfs_as_operations;
    } else {
        // inode is special
        init_special_inode(inode, inode->i_mode, rdev);
    }

    /*
        Now fill in the filesystem specific information.
        This is done by first obtaining the vvsfs_inode_info struct from
        the VFS inode using the VVSFS_I macro.
     */
    inode_info = VVSFS_I(inode);
    inode_info->i_db_count = 1;
    inode_info->i_data[0] = dno;
    for (i = 1; i < VVSFS_N_BLOCKS; ++i)
        inode_info->i_data[i] = 0;

    // Make sure you hash the inode, so that VFS can keep track of its "dirty"
    // status and writes it to disk if needed.
    insert_inode_hash(inode);

    // Mark the inode as "dirty". This will inform the VFS that this inode needs
    // to be written to disk. The procedure for writing to disk is implemented
    // in vvsfs_write_inode() (as part of the "super" operations).
    mark_inode_dirty(inode);

    LOG("vvsfs - new_inode - done");
    return inode;
}

// This is a helper function for the "create"
// inode operation. It adds a new entry to the
// list of directory entries in the parent
// directory.
static int vvsfs_add_new_entry(struct inode *dir,
                               struct dentry *dentry,
                               struct inode *inode) {
    struct vvsfs_inode_info *dir_info = VVSFS_I(dir);
    struct super_block *sb = dir->i_sb;
    struct vvsfs_dir_entry *dent;
    struct buffer_head *bh;
    int num_dirs;
    uint32_t d_pos, d_off, dno;
    int newblock;
    int raw_dno;

    // calculate the number of entries from the i_size
    // of the directory's inode.
    num_dirs = dir->i_size / VVSFS_DENTRYSIZE;
    if (num_dirs >= VVSFS_MAX_DENTRIES) {
        DEBUG_LOG("vvsfs - add_new_entry - exceeded max dentries %d >= %u, "
                  "(i_size: %lld)\n",
                  num_dirs,
                  (uint32_t)VVSFS_MAX_DENTRIES,
                  dir->i_size);
        return -ENOSPC;
    }

    // Calculate the position of the new entry within
    // the data blocks
    d_pos = num_dirs / VVSFS_N_DENTRY_PER_BLOCK;
    d_off = num_dirs % VVSFS_N_DENTRY_PER_BLOCK;
    DEBUG_LOG(
        "vvsfs - add_new_entry - position: %u, offset: %u\n", d_pos, d_off);

    /* If the block is not yet allocated, allocate it.
     */
    if (d_pos >= dir_info->i_db_count) {
        LOG("vvsfs - add_new_entry - add new data block "
            "for directory entry\n");
        newblock = vvsfs_assign_data_block(dir_info, sb, d_pos);
        if (newblock < 0) {
            DEBUG_LOG("vvsfs - add_new_entry - failed data block assignment\n");
            return newblock;
        }
        dno = (uint32_t)newblock;
    } else {
        if ((raw_dno = vvsfs_index_data_block(dir_info, sb, d_pos)) < 0) {
            DEBUG_LOG(
                "vvsfs - add_new_entry - failed get data block index %d\n",
                raw_dno);
            return raw_dno;
        }
        dno = (uint32_t)raw_dno;
    }
    /* Update the on-disk structure */
    LOG("vvsfs - add_new_entry - reading dno: %d, "
        "d_pos: %d, block: %d\n",
        dno,
        d_pos,
        vvsfs_get_data_block(dno));

    // Note that the i_data contains the data block
    // position within the data bitmap, This needs to
    // be converted to actual disk block position if
    // you want to read it, using
    // vvsfs_get_data_block().
    bh = READ_BLOCK_OFF(sb, dno);
    if (!bh) {
        DEBUG_LOG("vvsfs - add_new_entry - failed to read target data block\n");
        return -ENOMEM;
    }
    dent = READ_DENTRY(bh, d_off);
    strncpy(dent->name, dentry->d_name.name, dentry->d_name.len);
    dent->name[dentry->d_name.len] = '\0';
    dent->inode_number = inode->i_ino;
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);

    DEBUG_LOG("vvsfs - add_new_entry - directory "
              "entry (%s, %d) added to "
              "block %d\n",
              dent->name,
              dent->inode_number,
              vvsfs_get_data_block(dno));

    dir->i_size = (num_dirs + 1) * VVSFS_DENTRYSIZE;
    dir->i_blocks = dir_info->i_db_count * (VVSFS_BLOCKSIZE / VVSFS_SECTORSIZE);
    dir->i_ctime = dir->i_mtime = current_time(dir);
    mark_inode_dirty(dir);
    return 0;
}

// The "create" operation for inode.
// This is called when a new file/directory is
// created.
static int
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
vvsfs_create(struct user_namespace *namespace,
#else
vvsfs_create(struct mnt_idmap *namespace,
#endif
             struct inode *dir,
             struct dentry *dentry,
             umode_t mode,
             bool excl) {
    struct vvsfs_inode_info *dir_info;
    int ret;
    struct buffer_head *bh;
    struct inode *inode;

    LOG("vvsfs - create : %s\n", dentry->d_name.name);

    if (dentry->d_name.len > VVSFS_MAXNAME) {
        LOG("vvsfs - create - file name too long");
        return -ENAMETOOLONG;
    }

    dir_info = VVSFS_I(dir);
    if (!dir_info) {
        LOG("vvsfs - create - vi_dir null!");
        return -EINVAL;
    }

    // create a new inode for the new file/directory
    inode = vvsfs_new_inode(dir, mode, 0);
    if (IS_ERR(inode)) {
        LOG("vvsfs - create - new_inode error!");
        brelse(bh);
        return -ENOSPC;
    }

    // add the file/directory to the parent
    // directory's list of entries -- on disk.
    ret = vvsfs_add_new_entry(dir, dentry, inode);
    if (ret != 0) {
        DEBUG_LOG("vvsfs - create - failed to create new entry for intial data "
                  "block\n");
        vvsfs_drop_inode_link(inode);
        discard_new_inode(inode);
        return ret;
    }

    // attach the new inode object to the VFS
    // directory entry object.
    d_instantiate(dentry, inode);

    LOG("File created %ld\n", inode->i_ino);
    return 0;
}

/* Read all direct block dentries into memory for a given inode
 *
 * @vi: Inode information for target inode
 * @sb: Superblock of the filesystem
 * @data: Data array to write dentries into
 * @db_count: Number of data blocks
 *
 * @return: (int) 0 if successful, error otherwise
 */
static int vvsfs_read_dentries_direct(struct vvsfs_inode_info *vi,
                                      struct super_block *sb,
                                      bytearray_t data) {
    struct buffer_head *bh;
    int i;
    uint32_t db_count;
    DEBUG_LOG("vvsfs - read_dentries_direct\n");
    // Ensure that we do not read the last block which is an indirection pointer
    db_count = min((uint32_t)VVSFS_LAST_DIRECT_BLOCK_INDEX, vi->i_db_count);
    DEBUG_LOG("vvsfs - read_dentires_direct - reading %u data blocks\n",
              db_count);
    for (i = 0; i < db_count; ++i) {
        LOG("vvsfs - read_dentries_direct - reading dno: %d, disk block: %d",
            vi->i_data[i],
            vvsfs_get_data_block(vi->i_data[i]));
        bh = READ_BLOCK(sb, vi, i);
        if (!bh) {
            // Buffer read failed, no more data when we expected some
            DEBUG_LOG("vvsfs - read_dentries_direct - failed buffer read\n");
            return -EIO;
        }
        // Copy the dentry into the data array
        memcpy(data + i * VVSFS_BLOCKSIZE, bh->b_data, VVSFS_BLOCKSIZE);
        brelse(bh);
    }
    DEBUG_LOG("vvsfs - read_dentries_direct - done\n");
    return 0;
}

/* Read all indirect block dentries into memory for a given inode
 *
 * @vi: Inode information for target inode
 * @sb: Superblock of the filesystem
 * @data: Data array to write dentries into
 * @db_count: Number of data blocks
 *
 * @return: (int) 0 if successful, error otherwise
 */
static int vvsfs_read_dentries_indirect(struct vvsfs_inode_info *vi,
                                        struct super_block *sb,
                                        bytearray_t data) {
    struct buffer_head *i_bh;
    struct buffer_head *bh;
    int i;
    uint32_t db_count;
    uint32_t offset;
    DEBUG_LOG("vvsfs - read_dentries_indirect\n");
    i_bh = READ_BLOCK(sb, vi, VVSFS_LAST_DIRECT_BLOCK_INDEX);
    if (!i_bh) {
        // Buffer read failed, no more data when we expected some
        DEBUG_LOG("vvsfs - read_dentries_indirect - failed buffer read\n");
        return -EIO;
    }
    db_count = vi->i_db_count - VVSFS_LAST_DIRECT_BLOCK_INDEX;
    DEBUG_LOG("vvsfs - read_dentries_indirect - reading %u data blocks\n",
              db_count);
    for (i = 0; i < db_count; ++i) {
        offset =
            read_int_from_buffer(i_bh->b_data + (i * VVSFS_INDIRECT_PTR_SIZE));
        LOG("vvsfs - read_entries_indirect - reading dno: %d, disk block: %u\n",
            offset,
            vvsfs_get_data_block(offset));
        bh = READ_BLOCK_OFF(sb, offset);
        if (!bh) {
            // Buffer read failed, no more data when we expected some
            brelse(i_bh);
            DEBUG_LOG("vvsfs - read_dentries_indirect - failed buffer read\n");
            return -EIO;
        }
        // Copy the dentry into the data array
        memcpy(data + i * VVSFS_BLOCKSIZE, bh->b_data, VVSFS_BLOCKSIZE);
        brelse(bh);
    }
    brelse(i_bh);
    DEBUG_LOG("vvsfs - read_dentries_indirect - done\n");
    return 0;
}

// vvsfs_read_dentries - reads all dentries into memory for a given inode
//
// @dir: Directory inode to read from
// @num_dirs: (output) count of dentries
// @return: (char*) data buffer returned contains all dentry data, this *MUST*
//                  be freed after use via `kfree(data)`
//
// @return: (bytearray_t) data buffer returned contains all dentry data, this
//          *MUST* be freed after use via `kfree(data)`
bytearray_t vvsfs_read_dentries(struct inode *dir, int *num_dirs) {
    struct vvsfs_inode_info *vi;
    struct super_block *sb;
    int err;
    bytearray_t data;
    DEBUG_LOG("vvsfs - read_dentries\n");
    // Retrieve vvsfs specific inode data from dir inode
    vi = VVSFS_I(dir);
    // Calculate number of dentries
    *num_dirs = dir->i_size / VVSFS_DENTRYSIZE;
    // Retrieve superblock object for R/W to disk blocks
    sb = dir->i_sb;
    DEBUG_LOG("vvsfs - read_dentries - number of dentries to read %d - %d\n",
              *num_dirs,
              vi->i_db_count);
    // Read all dentries into mem
    data = kzalloc(vi->i_db_count * VVSFS_BLOCKSIZE, GFP_KERNEL);
    if (!data) {
        return ERR_PTR(-ENOMEM);
    }
    if ((err = vvsfs_read_dentries_direct(vi, sb, data))) {
        kfree(data);
        return ERR_PTR(err);
    }
    if (vi->i_db_count >= VVSFS_N_BLOCKS &&
        (err = vvsfs_read_dentries_indirect(
             vi, sb, data + VVSFS_BUFFER_INDIRECT_OFFSET))) {
        kfree(data);
        return ERR_PTR(err);
    }
    DEBUG_LOG("vvsfs - read_dentries - done");
    return data;
}

// vvsfs_lookup - A file/directory name in a directory. It basically attaches
// the inode
//                of the file to the directory entry.
static struct dentry *
vvsfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags) {
    int num_dirs;
    int i;
    struct inode *inode = NULL;
    struct vvsfs_dir_entry *dent;
    const char *target_name;
    int target_name_len;
    bytearray_t data;
    target_name = (char *)dentry->d_name.name;
    target_name_len = dentry->d_name.len;
    DEBUG_LOG("vvsfs - lookup\n");
    if (dentry->d_name.len > VVSFS_MAXNAME) {
        printk("vvsfs - lookup - file name too long");
        return ERR_PTR(-ENAMETOOLONG);
    }

    data = vvsfs_read_dentries(dir, &num_dirs);
    if (IS_ERR(data)) {
        int err = PTR_ERR(data);
        DEBUG_LOG("vvsfs - lookup - failed cached dentries read: %d\n", err);
        return ERR_PTR(err);
    }
    for (i = 0; i < num_dirs; ++i) {
        dent = READ_DENTRY_OFF(data, i);
        if (namecmp(dent->name, target_name, target_name_len)) {
            inode = vvsfs_iget(dir->i_sb, dent->inode_number);
            if (!inode) {
                DEBUG_LOG("vvsfs - lookup - failed to get inode: %u\n",
                          dent->inode_number);
                kfree(data);
                return ERR_PTR(-EACCES);
            }
            d_add(dentry, inode);
            break;
        }
    }
    kfree(data);
    DEBUG_LOG("vvsfs - lookup - done\n");
    return NULL;
}

// The `mkdir` operation for directory. It simply calls vvsfs_create, with the
// added flag of S_IFDIR (signifying this is a directory).
static int
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
vvsfs_mkdir(struct user_namespace *namespace,
#else
vvsfs_mkdir(struct mnt_idmap *namespace,
#endif
            struct inode *dir,
            struct dentry *dentry,
            umode_t mode) {
    return vvsfs_create(namespace, dir, dentry, mode | S_IFDIR, 0);
}

// The "link" operation.
// Takes an existing inode and new directory and entry
// It then copies points the new entry to the old
// inode and increases the link count
static int vvsfs_link(struct dentry *old_dentry,
                      struct inode *dir,
                      struct dentry *dentry) {
    struct vvsfs_inode_info *dir_info;
    int ret;
    struct inode *inode;

    LOG("vvsfs - link : %s\n", dentry->d_name.name);

    if (dentry->d_name.len > VVSFS_MAXNAME) {
        LOG("vvsfs - link - file name too long");
        return -ENAMETOOLONG;
    }

    dir_info = VVSFS_I(dir);
    if (!dir_info) {
        LOG("vvsfs - link - vi_dir null!");
        return -EINVAL;
    }

    inode = d_inode(old_dentry);

    // minix and ext2 update the ctime so I think its
    // correct
    inode->i_ctime = current_time(inode);

    // increase inode ref counts
    inode_inc_link_count(inode);
    ihold(inode);

    // add the file/directory to the parent
    // directory's list of entries -- on disk.
    ret = vvsfs_add_new_entry(dir, dentry, inode);
    if (ret != 0) {
        // error so decrease the ref counts
        inode_dec_link_count(inode);
        iput(inode);
        return ret;
    }

    d_instantiate(dentry, inode);

    LOG("Link created %ld\n", inode->i_ino);
    return 0;
}

/* Unlink a dentry from a given directory inode
 *
 * @dir: Directory to remove from
 * @dentry: Target to remove
 *
 * @return: (int) 0 if successfull, error otherwise
 */
static int vvsfs_unlink(struct inode *dir, struct dentry *dentry) {
    int err;
    struct bufloc_t loc;
    struct inode *inode = d_inode(dentry);
    DEBUG_LOG("vvsfs - unlink\n");
    if (dentry->d_name.len > VVSFS_MAXNAME) {
        printk("vvsfs - unlink - file name too long");
        return -ENAMETOOLONG;
    }

    err = vvsfs_find_entry(
        dir, dentry, BL_PERSIST_BUFFER | BL_PERSIST_DENTRY, &loc);
    if (err) {
        DEBUG_LOG("vvsfs - unlink - failed to find entry\n");
        return -ENOENT;
    }
    err = vvsfs_delete_entry_bufloc(dir, &loc);
    if (err) {
        DEBUG_LOG("vvsfs - unlink - failed to delete "
                  "entry\n");
        return err;
    }

    inode->i_ctime = dir->i_ctime;

    err = vvsfs_drop_inode_link(inode);
    if (err) {
        DEBUG_LOG("vvsfs - unlink - failed to free inode blocks\n");
        return err;
    }

    DEBUG_LOG("vvsfs - unlink - done\n");
    return err;
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

/* Remove a given entry from a given directory
 *
 * @dir: Inode representation of directory to remove
 * from
 * @dentry: Target entry to remove
 *
 * @return: (int) 0 if successful, error otherwise
 */
static int vvsfs_rmdir(struct inode *dir, struct dentry *dentry) {
    struct inode *inode = d_inode(dentry);
    int err = -ENOTEMPTY;
    if (!vvsfs_empty_dir(inode)) {
        LOG("vvsfs - rmdir - directory is not "
            "empty\n");
        return err;
    } else if ((err = vvsfs_unlink(dir, dentry))) {
        DEBUG_LOG("vvsfs - rmdir - unlink error: %d\n", err);
        return err;
    }
    inode->i_size = 0;
    DEBUG_LOG("vvsfs - rmdir - done\n");
    mark_inode_dirty(dir);
    mark_inode_dirty(inode);
    return err;
}

// The "symlink" operation.
// Creates a new inode and stores the symlink pointer in the data
static int
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
vvsfs_symlink(struct user_namespace *namespace,
#else
vvsfs_symlink(struct mnt_idmap *namespace,
#endif
              struct inode *dir,
              struct dentry *dentry,
              const char *symname) {
    struct vvsfs_inode_info *dir_info;
    int err;
    struct buffer_head *bh;
    struct inode *inode;

    DEBUG_LOG("vvsfs - symlink : %s\n", dentry->d_name.name);

    if (dentry->d_name.len > VVSFS_MAXNAME) {
        LOG("vvsfs - symlink - file name too long");
        return -ENAMETOOLONG;
    }

    dir_info = VVSFS_I(dir);
    if (!dir_info) {
        LOG("vvsfs - symlink - vi_dir null!");
        return -EINVAL;
    }

    // create a new inode for the new file/directory
    inode = vvsfs_new_inode(dir, S_IFLNK | S_IRWXUGO, 0);
    if (IS_ERR(inode)) {
        LOG("vvsfs - symlink - new_inode error!");
        brelse(bh);
        return -ENOSPC;
    }

    err = page_symlink(inode, symname, strlen(symname) + 1);
    if (err) {
        vvsfs_drop_inode_link(inode);
        discard_new_inode(inode);
        return err;
    }

    // add the file/directory to the parent directory's list
    // of entries -- on disk.
    err = vvsfs_add_new_entry(dir, dentry, inode);
    if (err != 0) {
        vvsfs_drop_inode_link(inode);
        discard_new_inode(inode);
        return err;
    }

    d_instantiate(dentry, inode);
    mark_inode_dirty(inode);

    // attach the new inode object to the VFS directory entry object.
    DEBUG_LOG("Symlink created %ld\n", inode->i_ino);
    return 0;
}

// The "mknod" function
// This is called when a new special file is created.
static int vvsfs_mknod(struct user_namespace *mnt_userns,
                       struct inode *dir,
                       struct dentry *dentry,
                       umode_t mode,
                       dev_t rdev) {
    int ret;
    struct inode *inode;
    struct vvsfs_inode_info *dir_info;
    struct buffer_head *bh;

    if (DEBUG)
        printk("vvsfs - mknod : %s\n", dentry->d_name.name);

    if (!old_valid_dev(rdev))
        return -EINVAL;

    if (dentry->d_name.len > VVSFS_MAXNAME) {
        printk("vvsfs - mknod - file name too long");
        return -ENAMETOOLONG;
    }

    dir_info = VVSFS_I(dir);
    if (!dir_info) {
        printk("vvsfs - mknod - vi_dir null!");
        return -EINVAL;
    }

    // create a new inode for the new file/directory
    inode = vvsfs_new_inode(dir, mode, rdev);
    if (IS_ERR(inode)) {
        printk("vvsfs - mknod - new_inode error!");
        brelse(bh);
        return -ENOSPC;
    }

    // add the file/directory to the parent directory's list
    // of entries -- on disk.
    ret = vvsfs_add_new_entry(dir, dentry, inode);
    if (ret != 0) {
        vvsfs_drop_inode_link(inode);
        discard_new_inode(inode);
        return ret;
    }

    // attach the new inode object to the VFS directory entry object.
    d_instantiate(dentry, inode);

    printk("vvsfs - mknod - created %ld\n", inode->i_ino);
    return 0;
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

    // Update inode create time
    existing_inode->i_ctime = current_time(existing_inode);

    // Drop the link count of the inode that was originally pointed to by this
    // dentry
    vvsfs_drop_inode_link(existing_inode);

    return 0;
}

/**
 * Rename old_dentry to new_dentry
 */
static int vvsfs_rename(struct user_namespace *namespace,
                        struct inode *old_dir,
                        struct dentry *old_dentry,
                        struct inode *new_dir,
                        struct dentry *new_dentry,
                        unsigned int flags) {
    int err;
    struct bufloc_t src_loc;
    struct inode *old_inode = d_inode(old_dentry);
    struct inode *new_inode = d_inode(new_dentry);

    DEBUG_LOG("vvsfs - rename\n");

    // We fail if any of these flags are used, because ext2 doesn't handle them
    // either. See this Ed post:
    // https://edstem.org/au/courses/12685/discussion/1652085?comment=3689645
    if (flags & (RENAME_EXCHANGE | RENAME_WHITEOUT)) {
        DEBUG_LOG("vvsfs - rename - RENAME_EXCHANGE or RENAME_WHITEOUT not "
                  "supported\n");
        return -EINVAL;
    }

    // From the man page: If  oldpath and newpath are existing hard links
    // referring to the same file, then rename() does nothing, and returns a
    // success status.
    if (new_inode && new_inode->i_ino == old_inode->i_ino) {
        DEBUG_LOG("vvsfs - rename - old and new are hard links to same file\n");
        return 0;
    }

    // Check the filename isn't too long
    if (new_dentry->d_name.len > VVSFS_MAXNAME) {
        DEBUG_LOG("vvsfs - rename - file name too long");
        return -ENAMETOOLONG;
    }

    // From the man page:
    // RENAME_NOREPLACE: Don't  overwrite  newpath  of the rename.  Return an
    // error if newpath already exists.
    if (new_inode && (flags & RENAME_NOREPLACE)) {
        return -EEXIST;
    }

    // From the man page: oldpath can specify a directory.  In this case,
    // newpath must either not exist, or it must specify an empty directory
    if (new_inode && S_ISDIR(old_inode->i_mode)) {
        // Destination is a non-empty directory
        if (S_ISDIR(new_inode->i_mode) && !vvsfs_empty_dir(new_inode)) {
            DEBUG_LOG(
                "vvsfs - rename - target exists and is non-empty directory\n");
            return -ENOTEMPTY;
        }

        // Destination is a file of any sort
        if (!S_ISDIR(new_inode->i_mode)) {
            DEBUG_LOG("vvsfs - rename - cannot overwrite non-directory with "
                      "directory\n");
            return -ENOTDIR;
        }
    }

    // From the Linux Programming Interface book, section 18.4:
    // If oldpath refers to a file other than a directory, then newpath can't
    // specify the pathname of a directory
    if (!S_ISDIR(old_inode->i_mode) && new_inode &&
        S_ISDIR(new_inode->i_mode)) {
        DEBUG_LOG("vvsfs - rename - cannot overwrite directory with file\n");
        return -EISDIR;
    }

    // Find the original dentry for the file
    err = vvsfs_find_entry(
        old_dir, old_dentry, BL_PERSIST_BUFFER | BL_PERSIST_DENTRY, &src_loc);
    if (err) {
        DEBUG_LOG("vvsfs - rename - failed to find entry\n");
        return -ENOENT;
    }

    // If there already exists a file at the destination, we need to overwrite
    // the dentry to point to the source inode
    if (new_inode) {
        err = vvsfs_dentry_exchange_inode(
            new_dir, new_dentry, new_inode, src_loc.dentry->inode_number);
        if (err) {
            DEBUG_LOG("vvsfs - rename - failed to exchange the inode of an "
                      "existing dentry\n");
            return err;
        }
    } else {
        // Copy the dentry to the new location
        // Note this automatically checks to see if the directory is full
        err = vvsfs_add_new_entry(new_dir, new_dentry, old_inode);
        if (err) {
            DEBUG_LOG(
                "vvsfs - rename - failed to create dentry in new location\n");
            brelse(src_loc.bh);
            return err;
        }
    }

    mark_inode_dirty(old_inode);

    // Delete the dentry from the old position
    err = vvsfs_delete_entry_bufloc(old_dir, &src_loc);
    if (err) {
        DEBUG_LOG("vvsfs - rename - failed to delete entry\n");
        return err;
    }

    DEBUG_LOG("vvsfs - rename - done\n");
    return 0;
}

const struct inode_operations vvsfs_dir_inode_operations = {
    .create = vvsfs_create,
    .lookup = vvsfs_lookup,
    .mkdir = vvsfs_mkdir,
    .link = vvsfs_link,
    .rmdir = vvsfs_rmdir,
    .unlink = vvsfs_unlink,
    .symlink = vvsfs_symlink,
    .mknod = vvsfs_mknod,
    .rename = vvsfs_rename,
};

const struct inode_operations vvsfs_symlink_inode_operations = {
    .get_link = page_get_link,
};
