#include <linux/types.h>



void write_int_to_buffer(char *buf, uint32_t data);
uint32_t read_int_from_buffer(char *buf);

// Forward declaration
static uint32_t vvsfs_get_data_block(uint32_t bno);

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
