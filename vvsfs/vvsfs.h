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
#define true 1
#define false 0
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
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

#define __KERNEL__
#ifdef __KERNEL__

#define VVSFS_SET_MAP_BIT 0x80

extern const struct address_space_operations vvsfs_as_operations;
extern const struct inode_operations vvsfs_file_inode_operations;
extern const struct file_operations vvsfs_file_operations;
extern const struct inode_operations vvsfs_dir_inode_operations;
extern const struct file_operations vvsfs_dir_operations;
extern const struct super_operations vvsfs_ops;
extern const struct inode_operations vvsfs_symlink_inode_operations;

// Avoid using char* as a byte array since some systems may have a 16 bit char
// type this ensures that any system that has 8 bits = 1 byte will be valid for
// byte array usage irrespective of char sizing.
typedef uint8_t *bytearray_t;

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
static uint32_t vvsfs_reserve_inode_block(uint8_t *map) {
    uint32_t i = vvsfs_find_free_block(map, VVSFS_IMAP_SIZE);
    if (i == 0)
        return 0;
    return BNO_TO_INO(i);
}

static void vvsfs_free_inode_block(uint8_t *map, uint32_t ino) {
    vvsfs_free_block(map, INO_TO_BNO(ino));
}

static uint32_t vvsfs_reserve_data_block(uint8_t *map) {
    return vvsfs_find_free_block(map, VVSFS_DMAP_SIZE);
}

static void vvsfs_free_data_block(uint8_t *map, uint32_t dno) {
    vvsfs_free_block(map, dno);
}

// get the disk block number for a given inode number
uint32_t vvsfs_get_inode_block(unsigned long ino) {
    return (VVSFS_INODE_BLOCK_OFF + INO_TO_BNO(ino) / VVSFS_N_INODE_PER_BLOCK);
}

// inode offset (in bytes) relative to the start of the block
uint32_t vvsfs_get_inode_offset(unsigned long ino) {
    return (INO_TO_BNO(ino) % VVSFS_N_INODE_PER_BLOCK) * VVSFS_INODESIZE;
}

// get the disk block number for a given logical data block number
uint32_t vvsfs_get_data_block(uint32_t bno) {
    return VVSFS_DATA_BLOCK_OFF + bno;
}

// A macro to extract a vvsfs_inode_info object from a VFS inode.
#define VVSFS_I(inode) (container_of(inode, struct vvsfs_inode_info, vfs_inode))

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

#endif
