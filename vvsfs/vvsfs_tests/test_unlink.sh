#!/bin/bash
source ./init.sh
source read_dir_entry_names.sh
log_header "Testing unlink"

# Setup
content="ABCDEFG"
echo "blah" > testdir/aaa
ln testdir/aaa testdir/bbb
ln testdir/aaa testdir/ccc
ln testdir/ccc testdir/ddd
echo "$content" > testdir/bbb
touch testdir/other.txt

./remount.sh

# ==== CORE BEHAVIOUR ====

# Remove regular file
rm testdir/other.txt
assert_eq "$(find testdir -type f -name "other.txt" | wc -l)" "0" "cannot remove a regular file"
check_log_success "Can remove a regular file"
assert_eq "$(stat -c %s testdir)" "512" "expected total size == 512"
check_log_success "Total block size of file system is correct after removal"

# Verify files can be resolved and unlinked
assert_eq "$(stat testdir/aaa -c %h)" "4" "expected link count == 4"
check_log_success "Link count is correct for hardlinked file (4)"
rm testdir/ddd
assert_eq "$(stat testdir/aaa -c %h)" "3" "expected link count == 3"
check_log_success "Removing a link correctly updates original file link count"
assert_eq "$(stat -c %s testdir)" "384" "expected total size == 384"
check_log_success "Total block size of file system is correct after unlink of hardlink"

./remount.sh

# Ensure persistence to disk through umount/mount
assert_eq "$(find testdir -type f | wc -l)" "3" "expected 3 files after remount"
check_log_success "Unlinked files don't persist after remount"
assert_eq "$(stat -c %s testdir)" "384" "expected total size == 384" 
check_log_success "Total block size if correct after remount"

# Ensure min link count is kept
rm testdir/ccc testdir/bbb
assert_eq "$(stat testdir/aaa -c %h)" "1" "expected link count == 1"
check_log_success "Removing last hardlink keeps correct base link count of 1"

# ==== BLOCK AND DENTRY HANDLING ====

# Reset
./create.sh

dentriesPerBlock=$((VVSFS_BLOCKSIZE / VVSFS_DENTRYSIZE))

# Dentry in non-last block
fullBlockAndThreeDentries=$((dentriesPerBlock + 3))
for (( i = 1; i <= fullBlockAndThreeDentries; i++)); do
    touch "testdir/file$i"
done
expected="file$fullBlockAndThreeDentries"
for (( i = 2; i < fullBlockAndThreeDentries; i++ )); do
    expected="$expected file$i"
done
rm "testdir/file1"
assert_eq "$(find testdir -type f -name file1 | wc -l)" "0" "Unable to relocate dentry from last block to file hole in previous block"
check_log_success "Dentry from last direct block can relocate to other direct block to fill hole from removal"
readDirEntryNames testdir
assert_eq "$names" "$expected" "Expected dentry in last block to be moved to first dentry place in first block"
check_log_success "Dentry ordering is updated to reflect relocation of last direct block dentry to fill hole"

# Non-last dentry in last block
firstDentryInLastBlock=$((dentriesPerBlock + 1))
expected="file$fullBlockAndThreeDentries"
for (( i = 2; i <= dentriesPerBlock; i++ )); do
    expected="$expected file$i"
done
expected="$expected file$((firstDentryInLastBlock + 1))"
rm "testdir/file$firstDentryInLastBlock"
assert_eq "$(find testdir -type f -name "file$firstDentryInLastBlock" | wc -l)" "0" "Expected removed first dentry in last block to not exist"
check_log_success "Non-last dentry in last direct block can be removed"
readDirEntryNames testdir
assert_eq "$names" "$expected" "Expected last dentry in last block to be moved to first dentry in last block"
check_log_success "Dentry ordering is udpated to reflect removed last block, non-last dentry removal"

# Last dentry in last block
firstDentryInLastBlock=$((dentriesPerBlock + 2))
expected="file$fullBlockAndThreeDentries"
for (( i = 2; i <= dentriesPerBlock; i++ )); do
    expected="$expected file$i"
done
rm "testdir/file$firstDentryInLastBlock"
assert_eq "$(find testdir -type f -name "file$firstDentryInLastBlock" | wc -l)" "0" "Expected removed first dentry in last block to not exist"
check_log_success "Last dentry in last block can be removed and block is deallocated"
readDirEntryNames testdir
assert_eq "$names" "$expected" "Expected last dentry in last block to be removed"
check_log_success "Dentry ordering is updated to reflect last dentry removal and last block deallocation"
