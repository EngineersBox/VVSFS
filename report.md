# Overview

This report discusses the implementation of several operations into VVSFS, the Very Very Simple File System. Our group completed all four baseline requirements (unlinking, renaming, inode stats, filesystem stats), the advanced task (indirect blocks), and two extensions (hard/soft links and block/character devices).

# Baseline

## Unlink Dentries and Removing Directories

### Unlink

Supporiting unlinking of dentries requires consideration around the behaviour of evicting entries from data blocks and potentially data blocks themselves.
In order to make this achievable, the unlink is split into two stages:
1. Find the dentry
2. Remove the dentry

Supporting find and remove operations is the `struct bufloc_t` implementation which stores block and dentry indices for ease of use. In addition to this, this
structure also has provisions to allow for the retention of the buffer object and dentry object (dependently) to aid in reducing the need for re-indexing into
buffers later (handled by flags `BL_PERSIST_BUFFER` and `BL_PERSIST_DENTRY`). These can also be resolved at a later time if necessary.

The method `vvsfs_find_dentry` is used to search through the data blocks and saturate a `bufloc_t` instance with the relevant information. This is then forwarded
to the `vvsfs_delete_entry` method to perform the actual removal. Search operations is merely a linear traversal of the blocks bound to the `inode`, attempting to
match the name and parent inode of a dentry to the target one.

Removing a dentry requires care to be taken around several cases for where the dentry is:

 1. Last dentry in the last block
 2. Non-last dentry in last block
 3. Any position dentry in non-last block

In the first case, we can simply evict the dentry (by zeroing it) and we are done. After that we consider if there are dentries left, if so we deallocate the block
and remove it from the `inode->i_data` array.

![Last block last dentry](./figures/direcct_last_last.png)

The second case requires moving the last dentry in the last block to fill the hole of the one being evicted. We do this with a memmove to overwrite the target dentry,
then zero the old location of the last dentry in the last block.

![Last block non-last dentry](./figures/direct_last_non_last.png)

In the last case, we need to move the last dentry from the last block to our current block to fill the hole. This requires multiple buffer locations to be open at once,
ensuring we move and then zero the old location of the last dentry in the last block.

![Non-last block non-last dentry](./figures/direct_non_last_non_last.png)

Once all shifting and operations have been performed with dentries and blocks, the inode matching the dentry is deallocated and returned to the available pool (bitmap).

### Directory Removal

In order to perform directory removal, we rely on the `vvsfs_unlink` operation implemented as detailed in the above section. But before performing an arbitrary unlink
on the dentry matching the directory there are checks needed. Specifically, we need to ensure that the dentry is a directory it is empty, this is done by traversing the dentries within the
directory and verifying that it is empty (only contains reserved entries). Reserved implies any of the following:

 1. Is either a `.` or `..` entry (not supported on disk, so not a problem but checked regardless), in the `..` case, we also verify that the parent inode matches the
    inode stored in the dentry.
 2. Reseved inode number, specifically `0` as the root.

If any of these checks fail, we return `-ENOTDIR` when not a directory and `-ENOTEMPTY` when it is not empty. Assuming these pass, we forward to the `vvsfs_unlink` call
targetting the dentry associated with the directory to be removed.

## Renaming

We implement rename operations for files and directories via the `vvsfs_rename` method. Whenever a user runs the `mv` command in their shell, it issues a `renameat2()` syscall, which the VFS resolves to our `vvsfs_rename` method via the `.rename` entry in the `vvsfs_dir_inode_operations` struct.

There are two main cases for the rename operation: those where the destination dentry exists (because there is already a file/folder with the same name in the destination folder), and those where the destination does not exist.

In the simple case, where the destination dentry is not pre-existing, we can simply add a new dentry to the destination folder (setting the inode number to be that of the file to rename), and deallocate the old dentry from the source folder. Since these operations of finding, adding and removing dentries from folders are the same as those needed for the `vvsfs_unlink` function, we reuse many of the helper functions which were previously defined (`vvsfs_find_entry`, `vvsfs_delete_entry_bufloc`, etc). This eliminates code duplication and reduces the chance of bugs, as the dentry addition/removal logic is now tested via a wider variety of usage patterns.

Additional complications can emerge when the destination dentry already exists. There are many rules which govern whether a rename operation is allowed to overwrite an existing file or directory (e.g. depending on whether the destination directoy is empty). We consulted the man page for `rename()` and Michael Kerrisk's book *"The Linux Programming Interface"* to gain a deep understanding of these requirements, and carefully implemented checks to prevent invalid renames. The process of actually renaming the file is relatively similar to beforehand, with the one exception being that it is also necessary to decrement the link count in the existing file's inode. If the destination dentry was they only hard link to the existing file, it's link count will now be zero, which will cause the file to be deleted. This process is illustrated in Figure 1 below.

![Renaming a file to overwrite an existing file at the destination](./figures/rename.png)

## Inode Attributes

We added support for storing `GID / UID / atime / ctime / mtime`. We acheived this by:

  1. Adding the fields to the `vvsfs_inode` structure.

  2. Loading the data within the `vvsfs_iget` method.
      - Following Minix / EXT2's lead we set the tv_nsec time to zero.

  3. Syncing the data to disk within the `vvsfs_write_inode` method.

  4. We chose to not implement `setattr / getattr` at this time since we didn't have anything meanful to change from the generic default function provided by the VFS.

Challenges implementing this feature:

  1. During initial development it was discovered that the filesystem was somehow relying on the order of the inital fields in the `vvsfs_inode`. Instead of properly resolving this issue we decided to store the new fields at the end of the struct.

  2. During testing it was discovered that the Linux kernel has measures to prevent disk trashing by not updating an inodes `atime` all the time. To override this and force the kernel to always update the times we added `strictatime` to our test mount script.

## Supporting FS Stats

TODO

# Advanced

## Indirect Blocks

There are three main areas that need to change to support indirect blocks, listing dentries, adding a new dentry and unlinking a dentry. In the first case of listing dentries,
this is the most straight forward. Instead of previously iterating over all the blocks in `i_data`, we traverse only the first 14 and load the dentries into memory,
then we check if a 15th exists (indicating indirect blocks are present), which is then buffered. Then we perform the same iteration over the block addresses within the
indirect block (via offset using `sizeof(uint32_t) * <iteration count>`). The iteration count (inclusive) is determined by subtracting 15 from the total block count.
We then traverse all dentries within each block, iterating 8 (dentries per block) times per block until the last entry where we use the total dentry cout modulo 8
as the limit. Note that due to the conventions of the code, there will never be a case where the indirect block is allocated with zero block entries within it.

When adding a new dentry, we first calculate the new block position and dentry offset using the following:

```c
uint32_t dentry_count = inode->i_size / VVSFS_DENTRYSIZE;
uint32_t new_dentry_block_pos = dentry_count / VVSFS_N_DENTRY_PER_BLOCK;
uint32_t new_dentry_block_off = dentry_count % VVSFS_N_DENTRY_PER_BLOCK;
```

If the `new_dentry_block_pos` is greater or equal to the current block count, we need to allocate a new block. This is done via `vvsfs_assign_data_block`, which
first determines if we are allocating a direct block, in which case it does and returns the new block address. If we are allocating an indirect block, we first
check if we have an indirect block allocated already, if we don't then it is created. Next we create a new block to store as the first indirect block and put
it's address in the indirect block. Lastly we return the first indirect block address.

If we don't need a new block, we call to `vvsfs_index_data_block` to get retrieve the data block address for the block associated wtih `new_dentry_block_pos`.
After this, we buffer the block, and write the dentry to the offset denoted by `new_dentry_block_off` which is guaranteed to be free, semantically guaranteed
by the logic flow that is.

From here on out, we initialise the dentry with the required data as necessary, name, inode number, etc.

Lastly, unlinking a dentry is a somewhat interesting set of changes. We need to extend the shifting behaviour to work with so-called spatially-disconnected
contiguous arrays. That is, the indirect and direct blocks are continugous notionally, as an array, whereby the indirect blocks require more calculation and
logic to index into. We need to support three main cases for dentry movement using indirect blocks:

 1. Direct only (using previous logic)
 2. Indirect only, dentry moves between indirect blocks
 3. Indirect to direct, dentry moves between indirect and direct blocks.

The first case is straightforward, as we can check the block count, if it is less than 15, we delegate to the old logic. In the second case, we check that the block
index (stored in `struct bufloc_t` passed as an argument from `vvsfs_find_entry`), is greater than or equal to 15, in which case we are only moving dentries within
indirect blocks. From here we apply the same logic as the first case, but operate over indirect blocks instead of direct blocks. In the third case, we need to apply
the same logic from the old direct-only implementation, but buffer both direct and indrect blocks at the same time.

![Indirect to indirect](./figures/indirect_to_indirect.png)

In the case and thirs cases, a situation exists where the only dentry of the last (but not only) indirect block is moved, requiring deallocation of that indirect
block but not the indirect addresses block.

![Indirect to direct](./figures/indirect_to_direct.png)

Again for the second and third cases, one last point of complexity is checking if we are moving the only dentry in the only indirect block to a direct block. In
this case we deallocate the first indirect block and the last direct block (indirect addresses block).

![Indirect last to direct](./figures/indirect_last_to_direct.png)

# Extensions

## Hardlinks and Symbolic Links

TODO

## Special Devices

TODO

# Testing

 * We created our own test suite (vvsfs/vvsfs_tests)
    - We used a test driven development methodology, where we would create tests for expected behaviour and build new features to make them pass.
    - Additionally we utilised this as a regression test suite to ensure that new code didn't break existing functionality. Furthermore, whenever we fixed problems that were discoved, we built a test to ensure that we didn't break it again.
    - The suite is composed of a set of helper scripts that provide automatic generation of a test environment, and an [assertion framework](https://github.com/torokmark/assert.sh/blob/main/assert.sh) to provide nice error messages.

 * We used the [pjdfstest](https://github.com/pjd/pjdfstest) filesystem test suite to check our implentation for POSIX compliance and various other edge cases. By the end we passed all tests with the following exceptions:
    1. The tests for large files (2gb) files.
    2. The filesystem does not keep track of the `.` & `..` files in directories. As such, we failed the test that checks whether folder link counts are incremented correctly. We chose to ignore this due to [a note posted by Alwen on the course Ed forum](https://edstem.org/au/courses/12685/discussion/1633469).
    3. The filesystem does not correctly update ctime on truncate. (TODO: Does anyone want to fix this?)
    4. The filesystem does not store high presision time, only seconds like minix & ext2. (TODO: Does anyone want to fix this?)

 * TODO: Should we discuss the rename bug?
