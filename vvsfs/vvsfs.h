#ifndef VVSFS_H
#define VVSFS_H

#define VVSFS_BLOCKSIZE 1024 // filesystem blocksize
#define VVSFS_SECTORSIZE 512 // disk sector size
#define VVSFS_INODESIZE 256  // inode size
#define VVSFS_N_INODE_PER_BLOCK                                                \
    ((VVSFS_BLOCKSIZE / VVSFS_INODESIZE)) // maximum number of inodes per block
#define VVSFS_N_BLOCKS 15
#define VVSFS_MAXBLOCKS 20484
#define VVSFS_IMAP_SIZE 512
#define VVSFS_DMAP_SIZE 2048
#define VVSFS_MAGIC 0xCAFEB0BA
#define VVSFS_INODE_BLOCK_OFF 4   // location of first inode
#define VVSFS_DATA_BLOCK_OFF 4100 // location of first data block
#define VVSFS_INDIRECT_PTR_SIZE ((sizeof(uint32_t)))
#define VVSFS_LAST_DIRECT_BLOCK_INDEX (VVSFS_N_BLOCKS - 1)
#define VVSFS_BUFFER_INDIRECT_OFFSET                                           \
    (VVSFS_LAST_DIRECT_BLOCK_INDEX * VVSFS_BLOCKSIZE)
#define VVSFS_MAX_INDIRECT_PTRS ((VVSFS_BLOCKSIZE / VVSFS_INDIRECT_PTR_SIZE))
#define VVSFS_MAX_INODE_BLOCKS ((VVSFS_N_BLOCKS - 1 + VVSFS_MAX_INDIRECT_PTRS))
#define VVSFS_IMAP_INODES_PER_ENTRY 8
#define VVSFS_MAX_INODE_ENTRIES (VVSFS_IMAP_SIZE * VVSFS_IMAP_INODES_PER_ENTRY)
#define VVSFS_MAX_DENTRIES ((VVSFS_N_DENTRY_PER_BLOCK * VVSFS_MAX_INODE_BLOCKS))
#define VVSFS_MAXFILESIZE ((VVSFS_BLOCKSIZE * VVSFS_MAX_INODE_BLOCKS))

#ifdef __KERNEL__
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
#else
#include <stdint.h>
#include <string.h>
#endif

#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif

/*

super block:    [info               ]          1 block (only 4 bytes used)
                [inode maps         ]          1 block (only 512 bytes used)
                [data block maps    ]          2 blocks
inode table:    [inode table        ]        512*8 blocks
data blocks:    [data blocks        ]       2048*8 blocks

inode size: 256 bytes

*/

struct vvsfs_inode {
    uint32_t i_mode;
    uint32_t i_size;        // Size in bytes
    uint32_t i_links_count; /* Links count */
    uint32_t
        i_data_blocks_count;          /* Data block counts, for block size
                                         VVSFS_BLOCKSIZE.     Note that this may not be
                                         the same as VFS inode i_blocks member. */
    uint32_t i_block[VVSFS_N_BLOCKS]; /* Pointers to blocks */
    uint32_t i_uid;                   // User id
    uint32_t i_gid;                   // Group id
    uint32_t i_atime;                 // Access time
    uint32_t i_mtime;                 // Modification time
    uint32_t i_ctime;                 // Creation time
    uint32_t i_rdev;                  // rdev stuff (for special files)
};

#define VVSFS_MAXNAME 123 // maximum size of filename

struct vvsfs_dir_entry {
    char name[VVSFS_MAXNAME + 1];
    uint32_t inode_number;
};

#define VVSFS_DENTRYSIZE ((sizeof(struct vvsfs_dir_entry)))
#define VVSFS_N_DENTRY_PER_BLOCK                                               \
    ((VVSFS_BLOCKSIZE /                                                        \
      VVSFS_DENTRYSIZE)) // maximum number of directory entries per block

/* Calculate the number of dentries in the last data
 * block
 *
 * @dir: Directory inode object
 * @count: Variable used to store count in
 *
 * @return: (int) count of dentries
 */
#define LAST_BLOCK_DENTRY_COUNT(dir, count)                                    \
    (count) = ((dir)->i_size / VVSFS_DENTRYSIZE) % VVSFS_N_DENTRY_PER_BLOCK;   \
    (count) = (count) == 0 ? VVSFS_N_DENTRY_PER_BLOCK : (count)

/* Determine if a given name and inode represent a
 * non-reserved dentry (e.g. not '.' or '..')
 *
 * @name: Name of the dentry
 * @inumber: Inode number of the dentry
 * @inode: Parent directory inode
 *
 * @return: (int) 1 if not reserved, 0 otherwise
 */
#define IS_NON_RESERVED_DENTRY(name, inumber, inode)                           \
    (inumber) != 0 &&                                                          \
        ((name)[0] != '.' || (!(name)[1] && (inumber) != (inode)->i_ino) ||    \
         (name)[1] != '.' || (name)[2])

#ifdef __KERNEL__

#define VVSFS_SET_MAP_BIT 0x80

// Operations
extern const struct address_space_operations vvsfs_as_operations;
extern const struct inode_operations vvsfs_file_inode_operations;
extern const struct file_operations vvsfs_file_operations;
extern const struct inode_operations vvsfs_dir_inode_operations;
extern const struct file_operations vvsfs_dir_operations;
extern const struct super_operations vvsfs_ops;
extern const struct inode_operations vvsfs_symlink_inode_operations;

// inode cache -- this is used to attach vvsfs specific inode
// data to the vfs inode
extern struct kmem_cache *vvsfs_inode_cache;

// Avoid using char* as a byte array since some systems may have a 16 bit char
// type this ensures that any system that has 8 bits = 1 byte will be valid for
// byte array usage irrespective of char sizing.
typedef uint8_t *bytearray_t;

// A "container" structure that keeps the VFS inode and additional on-disk data.
struct vvsfs_inode_info {
    uint32_t i_db_count;             /* Data blocks count */
    uint32_t i_data[VVSFS_N_BLOCKS]; /* Pointers to blocks */
    struct inode vfs_inode;
};

struct vvsfs_sb_info {
    uint64_t nblocks; /* max supported blocks */
    uint64_t ninodes; /* max supported inodes */
    uint8_t *imap; /* inode blocks map */
    uint8_t *dmap; /* data blocks map  */
};

/* Representation of a location of a dentry within
 * the data blocks.
 */
struct __attribute__((packed)) bufloc_t {
    int b_index;                    // Data block index
    int d_index;                    // Dentry index within data block
    unsigned flags;                 // Flags used to construct instance
    struct buffer_head *bh;         // Data block
    struct vvsfs_dir_entry *dentry; // Matched entry
};

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
extern int vvsfs_resolve_bufloc(struct inode *dir,
                         struct vvsfs_inode_info *vi,
                         struct bufloc_t *bufloc);

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
extern int vvsfs_index_data_block(struct vvsfs_inode_info *vi,
                                  struct super_block *sb,
                                  uint32_t d_pos);

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
extern int vvsfs_assign_data_block(struct vvsfs_inode_info *dir_info,
                                   struct super_block *sb,
                                   uint32_t d_pos);

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
extern int vvsfs_find_entry(struct inode *dir,
                     struct dentry *dentry,
                     unsigned flags,
                     struct bufloc_t *out_loc);

// Fill the super_block structure with information
// specific to vvsfs
extern int vvsfs_fill_super(struct super_block *s, void *data, int silent);

/* Free all data blocks in a given inode
 *
 * @inode: Target inode to free all indirect and direct
 *         data blocks
 *
 * @return: (int) 0 if successful, error otherwise
 */
extern int vvsfs_free_inode_blocks(struct inode *inode);

// vvsfs_read_dentries - reads all dentries into memory for a given inode
//
// @dir: Directory inode to read from
// @num_dirs: (output) count of dentries
// @return: (char*) data buffer returned contains all dentry data, this *MUST*
//                  be freed after use via `kfree(data)`
//
// @return: (bytearray_t) data buffer returned contains all dentry data, this
//          *MUST* be freed after use via `kfree(data)`
extern bytearray_t vvsfs_read_dentries(struct inode *dir, int *num_dirs);

// vvsfs_iget - get the inode from the super block
// This function will either return the inode that
// corresponds to a given inode number (ino), if it's
// already in the cache, or create a new inode object,
// if it's not in the cache. Note that this is very
// similar to vvsfs_new_inode, except that the
// requested inode is supposed to be allocated on-disk
// already. So don't use this to create a completely
// new inode that has not been allocated on disk.
extern struct inode *vvsfs_iget(struct super_block *sb, unsigned long ino);

// vvsfs_find_free_block
// @map:  the bitmap that keeps track of free blocks
// @size: the size of the bitmap.
//
// This function finds the first free block in a bitmap and toggles the
// corresponding bit in the bitmap. Note that block 0 is always reserved, so the
// search always start from 1. On success the position of the first free block
// is returned, and the bitmap is updated. On failure, the function returns 0
// and leave the bitmap unchanged. NOTE: the position returned is relative to
// the start of the map, so it is *not* the actual location on disk.
static uint32_t vvsfs_find_free_block(uint8_t *map, uint32_t size) {
    int i = 0;
    uint8_t j = 0;

    for (i = 0; i < size; ++i) {
        for (j = 0; j < 8; ++j) {
            if (i == 0 && j == 0)
                continue; // skip block 0 -- it is reserved.
            if ((~map[i]) & (VVSFS_SET_MAP_BIT >> j)) {
                map[i] = map[i] | (VVSFS_SET_MAP_BIT >> j);
                return i * 8 + j;
            }
        }
    }
    return 0;
}

static void vvsfs_free_block(uint8_t *map, uint32_t pos) {
    uint32_t i = pos / 8;
    uint8_t j = pos % 8;

    map[i] = map[i] & ~(VVSFS_SET_MAP_BIT >> j);
}

// mapping from position in an imap to inode number and vice versa.
// 0 is an invalid inode number
#define BNO_TO_INO(x) ((x + 1))
#define INO_TO_BNO(x) ((x - 1))
#define BAD_INO(x) ((x == 0))

// vvsfs_reserve_inode_block
// @map: a bitmap representing inode blocks on disk
//
// On success, this function returns a valid inode number and update the
// corresponding inode bitmap. On failure, it will return 0 (which is an invalid
// inode number), and leave the bitmap unchanged.
extern uint32_t vvsfs_reserve_inode_block(uint8_t *map);

__attribute__((always_inline))
static inline void vvsfs_free_inode_block(uint8_t *map, uint32_t ino) {
    vvsfs_free_block(map, INO_TO_BNO(ino));
}

__attribute__((always_inline))
static inline uint32_t vvsfs_reserve_data_block(uint8_t *map) {
    return vvsfs_find_free_block(map, VVSFS_DMAP_SIZE);
}

__attribute__((always_inline))
static inline void vvsfs_free_data_block(uint8_t *map, uint32_t dno) {
    vvsfs_free_block(map, dno);
}

// get the disk block number for a given inode number
__attribute__((always_inline))
static inline uint32_t vvsfs_get_inode_block(unsigned long ino) {
    return (VVSFS_INODE_BLOCK_OFF + INO_TO_BNO(ino) / VVSFS_N_INODE_PER_BLOCK);
}

// inode offset (in bytes) relative to the start of the block
__attribute__((always_inline))
static inline uint32_t vvsfs_get_inode_offset(unsigned long ino) {
    return (INO_TO_BNO(ino) % VVSFS_N_INODE_PER_BLOCK) * VVSFS_INODESIZE;
}

// get the disk block number for a given logical data block number
__attribute__((always_inline))
static inline uint32_t vvsfs_get_data_block(uint32_t bno) {
    return VVSFS_DATA_BLOCK_OFF + bno;
}

// A macro to extract a vvsfs_inode_info object from a VFS inode.
#define VVSFS_I(inode) (container_of(inode, struct vvsfs_inode_info, vfs_inode))

extern uint32_t vvsfs_get_data_block(uint32_t bno);
extern void write_int_to_buffer(char *buf, uint32_t data);
extern uint32_t read_int_from_buffer(char *buf);

#define READ_BLOCK_OFF(sb, offset)                                             \
    sb_bread((sb), vvsfs_get_data_block((offset)))
#define READ_BLOCK(sb, vi, index) READ_BLOCK_OFF(sb, (vi)->i_data[(index)])
#define READ_DENTRY_OFF(data, offset)                                          \
    ((struct vvsfs_dir_entry *)((data) + (offset)*VVSFS_DENTRYSIZE))
#define READ_DENTRY(bh, offset) READ_DENTRY_OFF((bh)->b_data, offset)
#define READ_INDIRECT_BLOCK(sb, indirect_bh, i)                                \
    READ_BLOCK_OFF(sb,                                                         \
                   read_int_from_buffer((indirect_bh)->b_data +                \
                                        ((i)*VVSFS_INDIRECT_PTR_SIZE)))

#endif
#endif // VVSFS_H