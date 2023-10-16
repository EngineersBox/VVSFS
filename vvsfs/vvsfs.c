/*
 * The Very Very Simple File System (vvsfs)
 * Eric McCreath 2006, 2008, 2010, 2020, 2023 - GPL
 * (based on the simplistic RAM filesystem McCreath 2001)
 *
 * Alwen Tiu, 2023. Added various improvements to allow multiple data blocks, bitmaps,
 * and using address space operations to simplify read/write. 
 */


#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/statfs.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/mpage.h>
#include <asm/uaccess.h>

#include "vvsfs.h"

#define DEBUG 1

// inode cache -- this is used to attach vvsfs specific inode
// data to the vfs inode
static struct kmem_cache * vvsfs_inode_cache; 

static struct address_space_operations vvsfs_as_operations; 
static struct inode_operations vvsfs_file_inode_operations;
static struct file_operations vvsfs_file_operations;
static struct inode_operations vvsfs_dir_inode_operations;
static struct file_operations vvsfs_dir_operations;
static struct super_operations vvsfs_ops;

struct inode *vvsfs_iget(struct super_block *sb, unsigned long ino);


// vvsfs_file_get_block 
// @inode: inode of the file 
// @iblock: the block number (relative to the beginning of the file) to be read. This is not
//          the actual disk block, but rather a logical block within a file. 
// @bh: the buffer to load the disk block to
// @create: allocate a block on disk if it's not already allocated. 
//
// This function translates a read operation for the "iblock"-th block 
// in a file to the actual read operation on disk. It is used by the readpage/writepage
// operations for pagecache. This allows a simpler and more modular implementation of 
// file read/write. All we need to do is to provide a primitive for reading one block
// at a time.  
// 
static int vvsfs_file_get_block(struct inode *inode,
                                   sector_t iblock,
                                   struct buffer_head *bh,
                                   int create)
{
    struct super_block *sb = inode->i_sb; 
    struct vvsfs_sb_info *sbi = sb->s_fs_info;
    struct vvsfs_inode_info *vi = VVSFS_I(inode); 
    uint32_t dno, bno; 

    if(DEBUG) printk("vvsfs - file_get_block"); 

    if(iblock >= VVSFS_N_BLOCKS)
        return -EFBIG;

    if(iblock > vi->i_db_count) return 0;

    if(iblock == vi->i_db_count)
    { 
        if(!create) 
            return 0;
        dno = vvsfs_reserve_data_block(sbi->dmap);
        if(dno==0)
            return -ENOSPC;
        vi->i_data[iblock] = dno; 
        vi->i_db_count++;
        inode->i_blocks = vi->i_db_count * VVSFS_BLOCKSIZE/VVSFS_SECTORSIZE; 
        bno = vvsfs_get_data_block(dno);         
    }
    else bno = vvsfs_get_data_block(vi->i_data[iblock]); 
 
    map_bh(bh, sb, bno); 
    return 0;
}


// Address pace operation readpage/readfolio. 
// You do not need to modify this. 
static int
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,19,0)
vvsfs_readpage(struct file *file, struct page *page)
{
    if(DEBUG) printk("vvsfs - readpage"); 
    return mpage_readpage(page, vvsfs_file_get_block); 
}
#else
vvsfs_read_folio(struct file *file, struct folio *folio) {
  if (DEBUG) printk("vvsfs - read folio");
  return mpage_read_folio(folio, vvsfs_file_get_block);
}
#endif

// Address pace operation readpage. 
// You do not need to modify this. 
static int vvsfs_writepage(struct page *page, struct writeback_control *wbc)
{
    if(DEBUG) printk("vvsfs - writepage"); 

    return block_write_full_page(page, vvsfs_file_get_block, wbc);
}

// Address pace operation readpage. 
// You do not need to modify this. 
static int vvsfs_write_begin(struct file *file,
                                struct address_space *mapping,
                                loff_t pos,
                                unsigned int len,
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,19,0)
                                unsigned int flags,
#endif
                                struct page **pagep,
                                void **fsdata)
{
    printk("vvsfs - write_begin"); 

    if (pos + len > VVSFS_MAXFILESIZE)
        return -ENOSPC;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,19,0)
    return block_write_begin(mapping, pos, len, flags, pagep,
                            vvsfs_file_get_block);
#else
    return block_write_begin(mapping, pos, len, pagep,
                            vvsfs_file_get_block);
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
                              void *fsdata)
{
    //struct inode *inode = file->f_inode;
    struct inode *inode = mapping->host;
    struct vvsfs_inode_info *vi = VVSFS_I(inode);
    int ret; 

    printk("vvsfs - write_end"); 

    ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);
    if (ret < len) {
        printk("wrote less than requested.");
        return ret;
    }

    /* Update inode metadata */
    inode->i_blocks = vi->i_db_count * VVSFS_BLOCKSIZE/VVSFS_SECTORSIZE;
    inode->i_mtime = inode->i_ctime = current_time(inode);
    mark_inode_dirty(inode);

    return ret;
}

static struct address_space_operations vvsfs_as_operations = {

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,19,0)
    readpage: vvsfs_readpage,
#else
    read_folio: vvsfs_read_folio,
#endif
    writepage: vvsfs_writepage,
    write_begin: vvsfs_write_begin,
    write_end: vvsfs_write_end,
};


// vvsfs_readdir - reads a directory and places the result using filldir
static int
vvsfs_readdir(struct file *filp, struct dir_context *ctx)
{
    struct inode *dir;
    struct vvsfs_inode_info *vi; 
    struct super_block * sb; 
    int num_dirs;
    struct vvsfs_dir_entry *dent;
    int i;
    struct buffer_head * bh; 
    char * data; 

    if (DEBUG) printk("vvsfs - readdir\n");

    // get the directory inode from file 
    dir = file_inode(filp);

    // get the vvsfs specific inode information from dir inode
    vi = VVSFS_I(dir);

    // calculate the number of entries in the directory
    num_dirs = dir->i_size / VVSFS_DENTRYSIZE;

    // get the superblock object from the inode; we'll need this
    // for reading/writing to disk blocks
    sb = dir->i_sb; 

    if (DEBUG) printk("Number of entries %d fpos %Ld\n", num_dirs, filp->f_pos);

    // Read all directory entries from disk to memory, and "emit" those entries
    // to dentry cache. 

    data = kzalloc(vi->i_db_count * VVSFS_BLOCKSIZE, GFP_KERNEL); 
    if(!data) return -ENOMEM; 

    for(i=0; i < vi->i_db_count; ++i)
    {
        printk("readdir - reading dno: %d, disk block: %d", 
            vi->i_data[i],
            vvsfs_get_data_block(vi->i_data[i]));
        bh = sb_bread(sb, vvsfs_get_data_block(vi->i_data[i]));
        if(!bh) {
            kfree(data);
            return -EIO;
        }
        memcpy(data+i*VVSFS_BLOCKSIZE, bh->b_data, VVSFS_BLOCKSIZE);
        brelse(bh); 
    }

    for(i=0; i < num_dirs && filp->f_pos < dir->i_size; ++i)
    {
        dent = (struct vvsfs_dir_entry *) (data + i*VVSFS_DENTRYSIZE); 
        if(!dir_emit(ctx, dent->name, strnlen(dent->name, VVSFS_MAXNAME), 
            dent->inode_number, DT_UNKNOWN))
        {
                if(DEBUG) printk("vvsfs -- readdir - failed dir_emit"); 
                break; 
        }
        ctx->pos += VVSFS_DENTRYSIZE;
    }
    kfree(data); 

    if(DEBUG) printk("vvsfs - readdir - done"); 

    return 0; 


}

// vvsfs_lookup - A file/directory name in a directory. It basically attaches the inode 
//                of the file to the directory entry.
static struct dentry *vvsfs_lookup(struct inode *dir,
                                   struct dentry *dentry,
                                   unsigned int flags)
{
    int num_dirs;
    int i;
    struct vvsfs_inode_info *vi; 
    struct inode *inode = NULL;
    struct vvsfs_dir_entry *dent;
    struct buffer_head *bh; 
    struct super_block *sb; 
    char * data; 

    if (DEBUG) printk("vvsfs - lookup\n");

    sb = dir->i_sb; 

    vi = VVSFS_I(dir); 
    num_dirs = dir->i_size/VVSFS_DENTRYSIZE;

    data = kzalloc(vi->i_db_count * VVSFS_BLOCKSIZE, GFP_KERNEL); 
    if(!data) return ERR_PTR(-ENOMEM); 

    for(i=0; i < vi->i_db_count; ++i)
    {
        printk("lookup - reading dno: %d, disk block: %d", 
            vi->i_data[i],
            vvsfs_get_data_block(vi->i_data[i]));

        bh = sb_bread(sb, vvsfs_get_data_block(vi->i_data[i]));
        if(!bh) {
            kfree(data);
            return ERR_PTR(-EIO);
        }
        memcpy(data+i*VVSFS_BLOCKSIZE, bh->b_data, VVSFS_BLOCKSIZE);
        brelse(bh); 
    }

    for(i=0; i < num_dirs; ++i)
    {
        dent = (struct vvsfs_dir_entry *) (data + i*VVSFS_DENTRYSIZE);

        if ((strlen(dent->name) == dentry->d_name.len) &&
            strncmp(dent->name,dentry->d_name.name,dentry->d_name.len) == 0) {
            inode = vvsfs_iget(dir->i_sb, dent->inode_number);
            if(!inode) 
            {
                return ERR_PTR(-EACCES); 
            }
            d_add(dentry, inode); 
            break; 
        }
    }
    kfree(data);
    return NULL; 

}


// vvsfs_new_inode - find and construct a new inode.
// @dir: the inode of the parent directory where the new inode is supposed to be attached to. 
// @mode: the mode information of the new inode
//
// This is a helper function for the inode operation "create" (implemented in vvsfs_create() ).
// It takes care of reserving an inode block on disk (by modifiying the inode bitmap), 
// creating an VFS inode object (in memory) and attach filesystem-specific information to 
// that VFS inode.
struct inode * vvsfs_new_inode(const struct inode * dir, umode_t mode)
{
    struct vvsfs_inode_info * inode_info; 
    struct super_block * sb;
    struct vvsfs_sb_info * sbi; 
    struct inode * inode;
    unsigned long dno, ino; 
    int i; 
    
    if (DEBUG) printk("vvsfs - new inode\n");

    // get the filesystem specific info for the super block. The sbi object contains
    // the inode bitmap. 
    sb = dir->i_sb;
    sbi = sb->s_fs_info; 

    /* 
        Find a spare inode in the vvsfs. 
        The vvsfs_reserve_inode_block() will attempt to find the first free inode
        and allocates it, and returns the inode number. 
        Note that the inode number is *not* the same as the disk block address on disk.
    */   
    ino = vvsfs_reserve_inode_block(sbi->imap);
    if(BAD_INO(ino)) return ERR_PTR(-ENOSPC); 

    /* 
        Find a spare data block. 
        By default, a new data block is reserved for the new inode. 
        This is probably a bit wasteful if the file/directory does not need it
        immediately. 
        The `dno` here represents a data block position within the data bitmap
        so it's not the actual disk block location.
     */
    dno = vvsfs_reserve_data_block(sbi->dmap);  
    if(dno == 0) {
        vvsfs_free_inode_block(sbi->imap, ino); // if we failed to allocate data block, release the inode block. 
                                                // and return an error code. 
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
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,3,0)
    inode_init_owner(&init_user_ns, inode, dir, mode);
#else
    inode_init_owner(&nop_mnt_idmap, inode, dir, mode);
#endif
    inode->i_ino = ino;
    inode->i_ctime = inode->i_mtime = inode->i_atime = current_time(inode);
    inode->i_mode = mode; 
    inode->i_size = 0; 
    inode->i_blocks = (VVSFS_BLOCKSIZE/VVSFS_SECTORSIZE); 
    // increment the link counter. This basically increments inode->i_nlink,
    // but that member cannot be modified directly. Use instead set_nlink to set it
    // to a specific value. 
    set_nlink(inode,1);

    // check if the inode is for a directory, using the macro S_ISDIR
    if(S_ISDIR(mode)) {
        inode->i_op = &vvsfs_dir_inode_operations; 
        inode->i_fop = &vvsfs_dir_operations; 
    }
    else {
        inode->i_op = &vvsfs_file_inode_operations; 
        inode->i_fop = &vvsfs_file_operations; 
        // if the inode is a file, set the address space operations
        inode->i_mapping->a_ops = &vvsfs_as_operations; 
    }

    /* 
        Now fill in the filesystem specific information. 
        This is done by first obtaining the vvsfs_inode_info struct from 
        the VFS inode using the VVSFS_I macro.
     */
    inode_info = VVSFS_I(inode);
    inode_info->i_db_count = 1; 
    inode_info->i_data[0] = dno; 
    for(i=1; i < VVSFS_N_BLOCKS; ++i)
        inode_info->i_data[i] = 0; 

    // Make sure you hash the inode, so that VFS can keep track of its "dirty" status
    // and writes it to disk if needed.
    insert_inode_hash(inode);
    
    // Mark the inode as "dirty". This will inform the VFS that this inode needs to be 
    // written to disk. The procedure for writing to disk is implemented in vvsfs_write_inode()
    // (as part of the "super" operations).
    mark_inode_dirty(inode); 


    if(DEBUG) printk("vvsfs - new_inode - done"); 
    return inode;
}


// This is a helper function for the "create" inode operation. It adds a new entry to the
// list of directory entries in the parent directory. 
static int vvsfs_add_new_entry(struct inode *dir, struct dentry *dentry, struct inode * inode)
{
    struct vvsfs_inode_info *dir_info = VVSFS_I(dir); 
    struct super_block *sb = dir->i_sb; 
    struct vvsfs_sb_info *sbi = sb->s_fs_info;
    struct vvsfs_dir_entry * dent; 
    struct buffer_head *bh; 
    int num_dirs; 
    uint32_t d_pos, d_off, dno, newblock; 

    // calculate the number of entries from the i_size of the directory's inode. 
    num_dirs = dir->i_size/VVSFS_DENTRYSIZE;
    if(num_dirs >= VVSFS_MAX_DENTRIES) return -ENOSPC;

    // Calculate the position of the new entry within the data blocks 
    d_pos = num_dirs/VVSFS_N_DENTRY_PER_BLOCK; 
    d_off = num_dirs % VVSFS_N_DENTRY_PER_BLOCK; 

    /* If the block is not yet allocated, allocate it. */
    if(d_pos >= dir_info->i_db_count)
    {
        printk("vvsfs - create - add new data block for directory entry"); 
        newblock = vvsfs_reserve_data_block(sbi->dmap); 
        if(newblock == 0) return -ENOSPC;
        dir_info->i_data[d_pos] = newblock; 
        dir_info->i_db_count++; 
    }

    /* Update the on-disk structure */

    dno = dir_info->i_data[d_pos]; 
    printk("vvsfs - add_new_entry - reading dno: %d, d_pos: %d, block: %d", dno, d_pos, 
        vvsfs_get_data_block(dno)); 

    // Note that the i_data contains the data block position within the data bitmap,
    // This needs to be converted to actual disk block position if you want to read it,
    // using vvsfs_get_data_block().  
    bh = sb_bread(sb, vvsfs_get_data_block(dno));  
    if(!bh) return -ENOMEM;
    dent = (struct vvsfs_dir_entry *) (bh->b_data + d_off * VVSFS_DENTRYSIZE); 
    strncpy(dent->name, dentry->d_name.name,dentry->d_name.len);
    dent->name[dentry->d_name.len] = '\0';
    dent->inode_number = inode->i_ino;
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh); 
    brelse(bh);

    if(DEBUG) 
        printk("vvsfs - add_new_entry - directory entry (%s, %d) added to block %d", 
                dent->name, dent->inode_number, vvsfs_get_data_block(dir_info->i_data[d_pos])); 
    
    dir->i_size = (num_dirs + 1) * VVSFS_DENTRYSIZE;
    dir->i_blocks = dir_info->i_db_count * (VVSFS_BLOCKSIZE/VVSFS_SECTORSIZE);
    mark_inode_dirty(dir); 
    return 0;
}

// The "create" operation for inode. 
// This is called when a new file/directory is created. 
static int
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,3,0)
vvsfs_create(struct user_namespace *namespace, 
#else
vvsfs_create(struct mnt_idmap *namespace,
#endif
                        struct inode *dir,
                        struct dentry* dentry,
                        umode_t mode,
                        bool excl)
{
    struct vvsfs_inode_info * dir_info; 
    int ret;
    struct buffer_head *bh; 
    struct inode * inode;

    if (DEBUG) printk("vvsfs - create : %s\n",dentry->d_name.name);

    if (dentry->d_name.len > VVSFS_MAXNAME) 
    {
        printk("vvsfs - create - file name too long");
        return -ENAMETOOLONG; 
    }

    dir_info = VVSFS_I(dir); 
    if(!dir_info) {
        printk("vvsfs - create - vi_dir null!");
        return -EINVAL;
    }

    // create a new inode for the new file/directory
    inode = vvsfs_new_inode(dir, mode);
    if (IS_ERR(inode)) {
        printk("vvsfs - create - new_inode error!"); 
        brelse(bh); 
        return -ENOSPC;
    }

    // add the file/directory to the parent directory's list
    // of entries -- on disk. 
    ret = vvsfs_add_new_entry(dir, dentry, inode); 
    if(ret != 0)
    {
        return ret; 
    }

    // attach the new inode object to the VFS directory entry object. 
    d_instantiate(dentry, inode);

    printk("File created %ld\n",inode->i_ino);
    return 0;

}



// The `mkdir` operation for directory. It simply calls vvsfs_create, with the
// added flag of S_IFDIR (signifying this is a directory). 
static int
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,3,0)
vvsfs_mkdir(struct user_namespace *namespace, 
#else
vvsfs_mkdir(struct mnt_idmap *namespace, 
#endif
                       struct inode *dir, 
                       struct dentry *dentry, 
                       umode_t mode)
{
    return vvsfs_create(namespace, dir, dentry, mode | S_IFDIR, 0);
}


// File operations; leave as is. We are using the generic VFS implementations
// to take care of read/write/seek/fsync. The read/write operations rely on the
// address space operations, so there's no need to modify these. 
static struct file_operations vvsfs_file_operations =
{
    llseek: generic_file_llseek,
    fsync:  generic_file_fsync, 
    read_iter: generic_file_read_iter,
    write_iter:  generic_file_write_iter,
};

static struct inode_operations vvsfs_file_inode_operations = {

};

static struct file_operations vvsfs_dir_operations =
{
    .llseek  = generic_file_llseek,
    .read    = generic_read_dir,
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 5, 0)
    .iterate = vvsfs_readdir,
#else
    .iterate_shared = vvsfs_readdir,
#endif
    .fsync   = generic_file_fsync,
};

static struct inode_operations vvsfs_dir_inode_operations = {
    create: vvsfs_create,           
    lookup: vvsfs_lookup,           
    mkdir:  vvsfs_mkdir, 
};


// This implements the super operation for writing a 'dirty' inode to disk 
// Note that this does not sync the actual data blocks pointed to by the inode;
// it only saves the meta data (e.g., the data block pointers, but not the actual
// data contained in the data blocks). Data blocks sync is taken care of by file
// and directory operations. 
static int vvsfs_write_inode(struct inode *inode,  struct writeback_control *wbc)
{
    struct super_block *sb;
    struct vvsfs_inode *disk_inode;
    struct vvsfs_inode_info *inode_info; 
    struct buffer_head *bh;
    uint32_t inode_block, inode_offset; 
    int i; 

    if (DEBUG) printk("vvsfs - write_inode");

    // get the vvsfs_inode_info associated with this (VFS) inode from cache. 
    inode_info = VVSFS_I(inode); 

    sb = inode->i_sb;
    inode_block = vvsfs_get_inode_block(inode->i_ino);
    inode_offset = vvsfs_get_inode_offset(inode->i_ino);

    printk("vvsfs - write_inode - ino: %ld, block: %d, offset: %d", inode->i_ino, inode_block, inode_offset);
    bh = sb_bread(sb,inode_block);
    if(!bh) return -EIO; 

    disk_inode = (struct vvsfs_inode *)((uint8_t *) bh->b_data + inode_offset);
    disk_inode->i_mode = inode->i_mode;
    disk_inode->i_size = inode->i_size; 
    disk_inode->i_data_blocks_count = inode_info->i_db_count;
    disk_inode->i_links_count = inode->i_nlink; 
    for(i=0; i < VVSFS_N_BLOCKS ; ++i)
        disk_inode->i_block[i] = inode_info->i_data[i]; 

    // TODO: if you have additional data added to the on-disk inode structure, you need
    // to sync it here. 

    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
    
    if (DEBUG) printk("vvsfs - write_inode done: %ld\n", inode->i_ino);
    return VVSFS_BLOCKSIZE;

}

// This function is needed to initiate the inode cache, to allow us to attach
// filesystem specific inode information. 
// You don't need to modify this. 
int vvsfs_init_inode_cache(void)
{
    if (DEBUG) printk("vvsfs - init inode cache ");

    vvsfs_inode_cache =  kmem_cache_create(
        "vvsfs_cache", sizeof(struct vvsfs_inode_info), 0, 0, NULL);
    if(!vvsfs_inode_cache)
        return -ENOMEM;
    return 0; 
}

// De-allocate the inode cache
void vvsfs_destroy_inode_cache(void)
{
    if (DEBUG) printk("vvsfs - destroy_inode_cache ");

    kmem_cache_destroy(vvsfs_inode_cache);
}

// This implements the super operation to allocate a new inode. 
// This will be called everytime a request to allocate a 
// new (in memory) inode is made. By default, this is handled by VFS,
// which allocates an VFS inode structure. But we override it here
// so that we can attach filesystem-specific information (.e.g,
// pointers to data blocks). 
// It is unlikely that you will need to modify this function directly;
// you can simply add whatever additional information you want to attach
// to the vvsfs_inode_info structure. 
static struct inode * vvsfs_alloc_inode(struct super_block *sb)
{
    struct vvsfs_inode_info *c_inode =
        kmem_cache_alloc(vvsfs_inode_cache, GFP_KERNEL);
    
    if (DEBUG) printk("vvsfs - alloc_inode ");

    if (!c_inode) return NULL;

    inode_init_once(&c_inode->vfs_inode);
    return &c_inode->vfs_inode;
}

// Deallocate the inode cache.
static void vvsfs_destroy_inode(struct inode *inode)
{
    struct vvsfs_inode_info *c_inode = container_of(inode, struct vvsfs_inode_info, vfs_inode);
    kmem_cache_free(vvsfs_inode_cache, c_inode);
}

// vvsfs_iget - get the inode from the super block
// This function will either return the inode that corresponds to a given inode number (ino),
// if it's already in the cache, or create a new inode object, if it's not in the cache.
// Note that this is very similar to vvsfs_new_inode, except that the requested inode
// is supposed to be allocated on-disk already. So don't use this to create a completely new
// inode that has not been allocated on disk. 
struct inode *vvsfs_iget(struct super_block *sb, unsigned long ino)
{
    struct inode *inode;
    struct vvsfs_inode * disk_inode;
    struct vvsfs_inode_info * inode_info; 
    struct buffer_head *bh; 
    uint32_t inode_block; 
    uint32_t inode_offset; 
    int i;

    if (DEBUG)
    {
        printk("vvsfs - iget - ino : %d", (unsigned int) ino);
        printk(" super %p\n", sb);
    }

    inode = iget_locked(sb, ino);
    if(!inode)
        return ERR_PTR(-ENOMEM);
    if(!(inode->i_state & I_NEW))
        return inode;

    inode_info = VVSFS_I(inode); 

    inode_block = vvsfs_get_inode_block(ino);
    inode_offset = vvsfs_get_inode_offset(ino); 

    bh = sb_bread(sb, inode_block); 
    if(!bh) {
        printk("vvsfs - iget - failed sb_read"); 
        return ERR_PTR(-EIO); 
    }

    disk_inode = (struct vvsfs_inode *)(bh->b_data + inode_offset);
    inode->i_mode = disk_inode->i_mode; 
    inode->i_size = disk_inode->i_size;

    // set the link count; note that we can't set inode->i_nlink directly; we
    // need to use the set_nlink function here. 
    set_nlink(inode, disk_inode->i_links_count); 
    inode->i_blocks = disk_inode->i_data_blocks_count * (VVSFS_BLOCKSIZE/VVSFS_SECTORSIZE); 
    
    inode_info->i_db_count = disk_inode->i_data_blocks_count; 
    /* store data blocks in cache */
    for(i=0; i < VVSFS_N_BLOCKS; ++i)
        inode_info->i_data[i] = disk_inode->i_block[i]; 

    // Currently we just filled the various time information with the current time,
    // since we don't keep this information on disk. You will need to change this
    // if you save this information on disk.
    inode->i_ctime = inode->i_mtime = inode->i_atime = current_time(inode);

    if (S_ISDIR(inode->i_mode))
    {
        inode->i_op = &vvsfs_dir_inode_operations;
        inode->i_fop = &vvsfs_dir_operations;
    }
    else
    {
        inode->i_op = &vvsfs_file_inode_operations;
        inode->i_fop = &vvsfs_file_operations;
        inode->i_mapping->a_ops = &vvsfs_as_operations; 
    }

    brelse(bh); 

    unlock_new_inode(inode);

    return inode;
}

// put_super is part of the super operations. This 
// is called when the filesystem is being unmounted, to deallocate
// memory space taken up by the super block info. 
static void vvsfs_put_super(struct super_block *sb)
{
    struct vvsfs_sb_info *sbi = sb->s_fs_info; 

    if (DEBUG)
        printk("vvsfs - put_super\n");
    
    if(sbi)
    {
        kfree(sbi->imap);
        kfree(sbi->dmap); 
        kfree(sbi);
    }

}

// statfs -- this is currently incomplete. 
// See https://elixir.bootlin.com/linux/v5.15.89/source/fs/ext2/super.c#L1407 
// for various stats that you need to provide. 
static int vvsfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
  if (DEBUG)
    printk("vvsfs - statfs\n");

  buf->f_namelen = VVSFS_MAXNAME;
  buf->f_type = VVSFS_MAGIC;
  buf->f_bsize = VVSFS_BLOCKSIZE; 

  // TODO: fill in other information about the file system. 

  return 0;
}

// Fill the super_block structure with information specific to vvsfs 
static int vvsfs_fill_super(struct super_block *s, void *data, int silent)
{
    struct inode *root_inode;
    int hblock;
    struct buffer_head *bh; 
    uint32_t magic; 
    struct vvsfs_sb_info * sbi; 

    if (DEBUG) printk("vvsfs - fill super\n");

    s->s_flags = ST_NOSUID | SB_NOEXEC;
    s->s_op = &vvsfs_ops;
    s->s_magic = VVSFS_MAGIC; 

    hblock = bdev_logical_block_size(s->s_bdev);
    if (hblock > VVSFS_BLOCKSIZE)
    {
        printk("vvsfs - device blocks are too small!!");
        return -1;
    }

    sb_set_blocksize(s, VVSFS_BLOCKSIZE); 

    /* Read first block of the superblock. 
        For this basic version, it contains just the magic number. */

    bh = sb_bread(s, 0); 
    magic = *((uint32_t *) bh->b_data); 
    if(magic != VVSFS_MAGIC) {
        printk("vvsfs - wrong magic number\n"); 
        return -EINVAL;
    }        
    brelse(bh); 

    /* Allocate super block info to load inode & data map */
    sbi = kzalloc(sizeof(struct vvsfs_sb_info), GFP_KERNEL); 
    if(!sbi) 
    {
        printk("vvsfs - error allocating vvsfs_sb_info");
        return -ENOMEM;
    }

    /* Load the inode map */
    sbi->imap = kzalloc(VVSFS_IMAP_SIZE, GFP_KERNEL);
    if(!sbi->imap) 
        return -ENOMEM; 
    bh = sb_bread(s, 1); 
    if(!bh) 
        return -EIO;
    memcpy(sbi->imap, bh->b_data, VVSFS_IMAP_SIZE); 
    brelse(bh); 

    /* Load the data map. Note that the data map occupies 2 blocks.  */
    sbi->dmap = kzalloc(VVSFS_DMAP_SIZE, GFP_KERNEL);
    if(!sbi->dmap) 
        return -ENOMEM; 
    bh = sb_bread(s, 2); 
    if(!bh) return -EIO;
    memcpy(sbi->dmap, bh->b_data, VVSFS_BLOCKSIZE); 
    brelse(bh); 
    bh = sb_bread(s, 3); 
    if(!bh) return -EIO; 
    memcpy(sbi->dmap + VVSFS_BLOCKSIZE, bh->b_data, VVSFS_BLOCKSIZE); 
    brelse(bh); 

    /* Attach the bitmaps to the in-memory super_block s */
    s->s_fs_info = sbi; 

    /* Read the root inode from disk */
    root_inode = vvsfs_iget(s, 1); 

    if(IS_ERR(root_inode))
    {
        printk("vvsfs - fill_super - error getting root inode"); 
        return PTR_ERR(root_inode); 
    }

    /* Initialise the owner */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,3,0)
    inode_init_owner(&init_user_ns, root_inode, NULL, root_inode->i_mode);
#else
    inode_init_owner(&nop_mnt_idmap, root_inode, NULL, root_inode->i_mode);
#endif
    mark_inode_dirty(root_inode); 

    s->s_root = d_make_root(root_inode);

    if(!s->s_root) {
        printk("vvsfs - fill_super - failed setting up root directory"); 
        iput(root_inode);
        return -ENOMEM; 
    }

    if (DEBUG) printk("vvsfs - fill super done\n");

    return 0;
}

// sync_fs super operation. 
// This writes super block data to disk. 
// For the current version, this is mainly the inode map and the data map.
static int vvsfs_sync_fs(struct super_block *sb, int wait)
{
    struct vvsfs_sb_info *sbi = sb->s_fs_info; 
    struct buffer_head *bh; 

    if(DEBUG) {
        printk("vvsfs -- sync_fs");
    }

    /* Write the inode map to disk */
    bh = sb_bread(sb, 1); 
    if(!bh) 
        return -EIO;
    memcpy(bh->b_data, sbi->imap, VVSFS_IMAP_SIZE); 
    mark_buffer_dirty(bh);
    if(wait) sync_dirty_buffer(bh); 
    brelse(bh); 

    /* Write the data map */

    bh = sb_bread(sb, 2); 
    if(!bh) return -EIO;
    memcpy(bh->b_data, sbi->dmap, VVSFS_BLOCKSIZE); 
    mark_buffer_dirty(bh); 
    if(wait) sync_dirty_buffer(bh); 
    brelse(bh); 

    bh = sb_bread(sb, 3); 
    if(!bh) return -EIO; 
    memcpy(bh->b_data, sbi->dmap + VVSFS_BLOCKSIZE,  VVSFS_BLOCKSIZE); 
    mark_buffer_dirty(bh);
    if(wait) sync_dirty_buffer(bh); 
    brelse(bh); 

    return 0;
}

static struct super_operations vvsfs_ops =
{
    statfs:         vvsfs_statfs,
    put_super:      vvsfs_put_super,
    alloc_inode:    vvsfs_alloc_inode,
    destroy_inode:  vvsfs_destroy_inode,
    write_inode:    vvsfs_write_inode, 
    sync_fs:        vvsfs_sync_fs,
};

// mounting the file system -- leave as is. 
static struct dentry *vvsfs_mount(struct file_system_type *fs_type,
                                  int flags,
                                  const char *dev_name,
                                  void *data)
{
    return mount_bdev(fs_type, flags, dev_name, data, vvsfs_fill_super);
}

static struct file_system_type vvsfs_type =
{
    .owner      = THIS_MODULE,
    .name       = "vvsfs",
    .mount      = vvsfs_mount,
    .kill_sb    = kill_block_super,
    .fs_flags   = FS_REQUIRES_DEV,
};

static int __init vvsfs_init(void)
{
    int ret = vvsfs_init_inode_cache();
    if (ret) {
        printk("inode cache creation failed");
        return ret; 
    }

    printk("Registering vvsfs\n");
    return register_filesystem(&vvsfs_type);
}

static void __exit vvsfs_exit(void)
{
    printk("Unregistering the vvsfs.\n");
    unregister_filesystem(&vvsfs_type);
    vvsfs_destroy_inode_cache();
}

module_init(vvsfs_init);
module_exit(vvsfs_exit);
MODULE_LICENSE("GPL");
