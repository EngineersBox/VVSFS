# VVSFS -- A very, very simple file system

This filesystem is a very much slimmed down and simplified version of `ext2` file system. It is designed for pedagogical purposes, to demonstrate how some critical components of a filesystesm work in the Linux operating system. 

## Compiling and running

* To compile, simply run `make`. This will create a kernel module `vvsfs.ko`. 

* To install the module, run `sudo insmod vvsfs.ko`. 

* To test the file system, first create a disk image file:

    ```
    dd if=/dev/zero of=mydisk.img bs=1024 count=20484
    ./mkfs.vvsfs mydisk.img 
    ```

* Mount the disk image, create a directory, say `testdir`

    ```
    sudo mount -o loop -t vvsfs mydisk.img testdir
    ```



## The on-disk organisation 

VVSFS uses the following overall disk layout:


[super block][inode map][data map][inode table][data blocks]

This is a simplication of `ext2` layout, i.e., it's essentially `ext2` without the cylinder groups. 


In the current version, VVSFS fixes a number of parameters to some pre-determined constants. This is to make it easier to work with various filesystem structures. 

Instead of fixing the disk size and then work out the rest of the parameters, we start by fixing two parameters: 

* Blocksize: VVSFS uses the block size of 1024 bytes (1KB). This was intentionally chosen to so as to make it easier to use a small `loop` device to test this file system.

* Inode size: 256 bytes; so each block will fit 4 inodes.

The various parameters for the disk structure were then basically decided starting from the inode bitmap and the data bitmap. 

The more precise on-disk structure is thus as follows:

```
super block:    [info               ]          1 block (only 4 bytes used)
                [inode maps         ]          1 block (only 512 bytes used)
                [data block maps    ]          2 blocks 
inode table:    [inode table        ]       4096 blocks
data blocks:    [data blocks        ]      16384 blocks
```

Since the inode bitmap is 512 bytes, it can encode the allocation status of up to `512*8 = 4096` inodes. Similarly, the 2KB data blocks bitmap can encode up to `2048*8 = 16384` data blocks. 


## VFS operations

In this simple (and incomplete) implementation some basic VFS operations are implemented. These cover the directory operations for creating files/directories, and the file operations for reading/writing to files.

There are some major differences between this version of VVSFS and the older one that you have seen in the lab: 

* The first is that we implemented some super operations as well. This is because we now have some on-disk data for the super block (mainly the bitmaps).

* The on-disk inode structure now stores pointers to data blocks. More precisely, each inode can have up to 15 data block pointers, so the maximum file size is 15 * 1024 bytes = 15KB. 

* The file operations for read/write are now replaced by generic file operations, and the actual reading/writing is managed through the `address_space` object. This makes it easier to implement read/write to files -- we only need to implement a mapping from a position of a block in a file (i.e., block number relative to the start of the file) to its actual location on disk. The VFS infrastructure will then take care of performing the file read/write operations. 

## Testing 

* To test the file/directory creation (without any data), the `touch` command can be quite useful. 
  
* To create small files (a few characters), a simple chain of commands like `echo hello > file1.txt` can be used. 

* The dd command can be used to create larger files, e.g., `dd if=/dev/zero of=testfile bs=1024 count=15` will create the largest allowed file in VVSFS. 

* To test various edge cases, it's best to work out first various limits of the filesystem (e.g., maximum file size, maximum number of directories, etc), and then write a shell script to automate things. 




## Some useful tips

Some general tips when working with VVSFS: 

- Inodes and bitmaps are cached in memory, but data blocks are not. So generally to update inodes and bitmaps, you don't have to directly write them to disk at the point where you modify them. Instead, these in-memory structures can be marked as "dirty" (for the case of inodes). VFS will then run the appropriate functions to flush these to the disk. For inodes, that function is the `vvsfs_write_inode`, and for super blocks it is `vvsfs_sync_fs`. So you only need to write the flushing functionalities for the meta data in one place rather than in several places. 

- For anything that resides in the data blocks you generally need to write to the disk explicitly at the point of modification. Technically, you are not writing directly to the disk, but to a buffer (the `buffer_head` structure). Reading and writing to disks are always done per block (1KB).  

- To read/write to disk blocks explicitly, you will generally execute the following sequence:

    * Use sb_bread() to obtain a buffer_head object associated with the filesystem. This also 'locks' the buffer to avoid concurrent modification. 
    * For reading: copy the b_data member to your target (local) buffer. For modification, you will copy a local buffer into b_data, and call 'mark_buffer_dirty()'. Don't worry about writing to the actual disks -- VFS takes care of that for you!
    * Release the buffer_head object using brelse(). 
    * Do what you want with the local buffer. 

The sb_bread() reads only one block at a time. It takes two arguments: a pointer to the super_block structure (not to be confused with the superblock blocks on disk), and the logical block number on disk. 

- From time to time, you may need to look at what's in a particular struct, or what an VFS internal function does. A useful website to navigate through linux kernel code is [https://elixir.bootlin.com/linux/latest/source](https://elixir.bootlin.com/linux/latest/source).


## Useful resources

- Looking at how `ext2` get things done can be quite helpful. You can find the `ext2` source code in the kernel source at [/fs/ext2](https://elixir.bootlin.com/linux/v5.15.84/source/fs/ext2). 

- There are a number of open source simple filesystem implementation on the web that you can use to learn more about VFS. One such implementation, that is based on a simplified ext2-like filesystem, is the [simplefs](https://github.com/sysprog21/simplefs). 
  

