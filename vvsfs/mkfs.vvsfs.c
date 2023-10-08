/*
 *  mkfs.vvsfs - constructs an initial empty file system
 * Eric McCreath 2006 GPL
 * Alwen Tiu 2023 GPL
 *
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "vvsfs.h"

char *device_name;
int device;

static void die(char *mess) {
    fprintf(stderr, "Exit : %s\n", mess);
    exit(1);
}

static void usage(void) { die("Usage : mkfs.vvsfs <device name>)"); }

static void write_disk(off_t *pos, uint8_t *block, off_t size) {
    if (*pos != lseek(device, *pos, SEEK_SET))
        die("seek set failed 1");

    if (write(device, block, size) != size)
        die("write failed 1");
    *pos += size;
}

int main(int argc, char **argv) {
    int i;

    uint8_t block[VVSFS_BLOCKSIZE];
    uint8_t magic[VVSFS_BLOCKSIZE];
    uint8_t imap[VVSFS_BLOCKSIZE];
    uint8_t dmap[VVSFS_DMAP_SIZE];

    struct vvsfs_inode inode;

    if (argc != 2)
        usage();

    // open the device for reading and writing
    device_name = argv[1];
    device = open(device_name, O_RDWR);

    off_t pos = 0;

    printf("Writing magic number\n");
    // set magic number for the first block
    memset(magic, 0, VVSFS_BLOCKSIZE);
    uint32_t *m = (uint32_t *)magic;
    *m = VVSFS_MAGIC;
    write_disk(&pos, magic, VVSFS_BLOCKSIZE);

    printf("Writing inode bitmap\n");
    // initialise inode map -- mark first block as taken
    memset(imap, 0, VVSFS_BLOCKSIZE);
    imap[0] = 1 << 7;
    write_disk(&pos, imap, VVSFS_BLOCKSIZE);

    printf("Writing data bitmap\n");
    // initialise data blocks map -- mark first block as taken
    memset(dmap, 0, VVSFS_DMAP_SIZE);
    dmap[0] = 1 << 7;
    write_disk(&pos, dmap, VVSFS_DMAP_SIZE);

    printf("Writing root inode\n");

    // Root inode: occupies first data block
    memset(block, 0, VVSFS_BLOCKSIZE);
    for (i = 0; i < VVSFS_N_BLOCKS; ++i)
        inode.i_block[i] = 0;
    inode.i_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP |
                   S_IWOTH | S_IXUSR | S_IXGRP | S_IXOTH;
    printf("Mode: %d\n", inode.i_mode);
    inode.i_data_blocks_count = 1;
    inode.i_links_count = 1;
    inode.i_size = 0;
    memcpy(block, &inode, sizeof(struct vvsfs_inode));
    write_disk(&pos, block, VVSFS_BLOCKSIZE);

    // zero remaining blocks
    printf("Zeroing remaining blocks\n");
    memset(block, 0, VVSFS_BLOCKSIZE);
    for (i = 5; i < VVSFS_MAXBLOCKS; i++)
        write_disk(&pos, block, VVSFS_BLOCKSIZE);

    close(device);
    printf("Done\n");

    return 0;
}
