#!/bin/bash
source init.sh
log_header "Testing statfs"

# Init
./create.sh

touch testdir/file{000..100}

# File name %n
assert_eq "$(stat -f -c %n testdir)" "testdir" "file/directory name incorrect"
check_log_success "File/directory name correct"
# Max name len %l
assert_eq "$(stat -f -c %l testdir)" "$VVSFS_MAXNAME" "max name length incorrect"
check_log_success "Max name length correct"
# FS type (decimal) %T
assert_eq "$(stat -f -c %t testdir)" "$(printf "%x\n" "$VVSFS_MAGIC")" "filesystem type/magic incorrect"
check_log_success "File system type/magic string correct"
# Block size (transfers) %s
assert_eq "$(stat -f -c %s testdir)" "$VVSFS_BLOCKSIZE" "transfer block size incorrect"
check_log_success "Transfer block size correct"
# Block size (fundamental) %S
assert_eq "$(stat -f -c %S testdir)" "$VVSFS_BLOCKSIZE" "fundamental block size incorrect"
check_log_success "Fundamental block size correct"
# Total blocks %b
assert_eq "$(stat -f -c %b testdir)" "$VVSFS_MAXBLOCKS" "total block count incorrect"
check_log_success "Total block count correct"
# Free blocks %f
assert_eq "$(stat -f -c %f testdir)" "16270" "free block count incorrect"
check_log_success "Free block count correct"
# Free blocks (nono-sudo) %a
assert_eq "$(stat -f -c %a testdir)" "16270" "non-superuser free block count incorrect"
check_log_success "Non-superuser free block count correct"
# Total inodes %c
assert_eq "$(stat -f -c %c testdir)" "$VVSFS_MAX_INODE_ENTRIES" "total inode count incorrect"
check_log_success "Total inode count correct"
# Free inodes %d
assert_eq "$(stat -f -c %d testdir)" "3994" "free inode count incorrect"
check_log_success "Free inode count correct"

./remount.sh

# File name %n
assert_eq "$(stat -f -c %n testdir)" "testdir" "file/directory name incorrect after remount"
check_log_success "File/directory name correct after remount"
# Max name len %l
assert_eq "$(stat -f -c %l testdir)" "$VVSFS_MAXNAME" "max name length incorrect after remount"
check_log_success "Max name length correct after remount"
# FS type (decimal) %T
assert_eq "$(stat -f -c %t testdir)" "$(printf "%x\n" "$VVSFS_MAGIC")" "filesystem type/magic incorrect after remount"
check_log_success "File system type/magic string correct after remount"
# Block size (transfers) %s
assert_eq "$(stat -f -c %s testdir)" "$VVSFS_BLOCKSIZE" "transfer block size incorrect after remount"
check_log_success "Transfer block size correct after remount"
# Block size (fundamental) %S
assert_eq "$(stat -f -c %S testdir)" "$VVSFS_BLOCKSIZE" "fundamental block size incorrect after remount"
check_log_success "Fundamental block size correct after remount"
# Total blocks %b
assert_eq "$(stat -f -c %b testdir)" "$VVSFS_MAXBLOCKS" "total block count incorrect after remount"
check_log_success "Total block count correct after remount"
# Free blocks %f
assert_eq "$(stat -f -c %f testdir)" "16270" "free block count incorrect after remount"
check_log_success "Free block count correct after remount"
# Free blocks (nono-sudo) %a
assert_eq "$(stat -f -c %a testdir)" "16270" "non-superuser free block count incorrect after remount"
check_log_success "Non-superuser free block count correct after remount"
# Total inodes %c
assert_eq "$(stat -f -c %c testdir)" "$VVSFS_MAX_INODE_ENTRIES" "total inode count incorrect after remount"
check_log_success "Total inode count correct after remount"
# Free inodes %d
assert_eq "$(stat -f -c %d testdir)" "3994" "free inode count incorrect after remount"
check_log_success "Free inode count correct after remount"

