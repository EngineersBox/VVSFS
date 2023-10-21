#include "buffer_utils.h"

/* Compare the names of dentries, checks length before comparing
 * name character entries.
 *
 * @name: Dentry name to compare against
 * @target_name: Dentry name that is being searched for
 * @target_name_len: Length of target_name string
 *
 * @return (int) 1 if names match, 0 otherwise
 */
__attribute__((always_inline)) static inline bool
namecmp(const char *name, const char *target_name, int target_name_len) {
    return strlen(name) == target_name_len &&
           strncmp(name, target_name, target_name_len) == 0;
}

/* Representation of a location of a dentry within
 * the data blocks.
 */
typedef struct __attribute__((packed)) bufloc_t {
    int b_index;                    // Data block index
    int d_index;                    // Dentry index within data block
    unsigned flags;                 // Flags used to construct instance
    struct buffer_head *bh;         // Data block
    struct vvsfs_dir_entry *dentry; // Matched entry
} bufloc_t;

/* Persist the struct buffer_head object in bufloc_t
 * without releasing it */
#define BL_PERSIST_BUFFER (1 << 1)
/* Persist (not clone) the struct vvsfs_dir_entry
 * object in bufloc_t, dependent on BL_PERSIST_BUFFER
 */
#define BL_PERSIST_DENTRY (1 << 2)
/* Determine if a given flag is set */
#define bl_flag_set(flags, flag) ((flags) & (flag))

/* Resolves the buffer head and dentry for a given
 * bufloc if they have not already been. This
 * behaviour is conditional on the flags set.
 *
 * Note that the user is expected to release the
 * buffer_head (bh field) with brelse(bufloc->bh) when
 * this bufloc_t instance is not longer needed. Since
 * the dentry field is constructed from the memory
 * held in the buffer_head data, it is no necessery to
 * release the dentry field.
 *
 * @dir: Target directory inode
 * @vi: Inode information for target directory inode
 * @bufloc: Specification of dentry location (does not
 * assume already resolved)
 *
 * @return: (int) 0 if successful, error otherwise
 */
static int vvsfs_resolve_bufloc(struct inode *dir,
                                struct vvsfs_inode_info *vi,
                                struct bufloc_t *bufloc) {
    struct buffer_head *i_bh;
    uint32_t index;
    if (bufloc == NULL) {
        return -EINVAL;
    }
    if (!bl_flag_set(bufloc->flags, BL_PERSIST_BUFFER)) {
        DEBUG_LOG("vvsfs - resolve_bufloc - bufloc has no peristed buffer, "
                  "resolving\n");
        if (bufloc->b_index < VVSFS_LAST_DIRECT_BLOCK_INDEX) {
            bufloc->bh = READ_BLOCK(dir->i_sb, vi, bufloc->b_index);
        } else {
            i_bh = READ_BLOCK(dir->i_sb, vi, VVSFS_LAST_DIRECT_BLOCK_INDEX);
            if (!i_bh) {
                return -EIO;
            }
            index = read_int_from_buffer(
                i_bh->b_data +
                (bufloc->b_index - VVSFS_LAST_DIRECT_BLOCK_INDEX) *
                    VVSFS_INDIRECT_PTR_SIZE);
            bufloc->bh = READ_BLOCK_OFF(dir->i_sb, index);
        }
        if (!bufloc->bh) {
            // Buffer read failed, something has
            // changed unexpectedly
            return -EIO;
        }
    }
    if (!bl_flag_set(bufloc->flags, BL_PERSIST_DENTRY)) {
        DEBUG_LOG("vvsfs - resolve_bufloc - bufloc has no persisted dentry, "
                  "resolving\n");
        bufloc->dentry = READ_DENTRY(bufloc->bh, bufloc->d_index);
    }
    return 0;
}
