#!/bin/bash

source init.sh
source ./read_dir_entry_names.sh

log_header "Test inode indirect pointers"

# Fill into indirect pointer block
direct_and_one_indirect_dentries=$((VVSFS_N_DENTRY_PER_BLOCK * VVSFS_N_BLOCKS))
# Includes 1.5 indirect blocks
count=$(($direct_and_one_indirect_dentries + (VVSFS_N_DENTRY_PER_BLOCK / 2)))
expected="file0"
for (( i = 0; i < count; i++)); do
    touch "testdir/file$i"
    if (( i > 0 )); then
        expected="$expected file$i"
    fi
done
readDirEntryNames testdir
assert_eq "$names" "$expected" "expected all $count files to exist"
check_log_success "Created $count files/dentries with $((VVSFS_N_BLOCKS - 1)) direct blocks and 2 indirect blocks"

# Can move between indirect blocks
first_dentry_first_indirect=$(((VVSFS_N_DENTRY_PER_BLOCK * VVSFS_LAST_DIRECT_BLOCK_INDEX) + 1))
rm "testdir/file$first_dentry_first_indirect"
assert_eq "$(find testdir -type f -name "file$first_dentry_first_indirect" | wc -l)" "0" "expected first dentry of first indirect block to not exist"
expected="file0"
for (( i = 1; i < first_dentry_first_indirect; i++)); do
    expected="$expected file$i"
done
expected="$expected file$((count - 1))"
for ((i = first_dentry_first_indirect + 1; i < count - 1; i++)); do
    expected="$expected file$i"
done
readDirEntryNames testdir
assert_eq "$names" "$expected" "expected file$first_dentry_first_indirect to be replaced by file$((count - 1)) and dentry naming to be consistent"
check_log_success "Moved dentry between indirect blocks to fill hole from removed dentry"

# Ensure that unlinks can move blocks from indirect to direct
rm testdir/file0
assert_eq "$(find testdir -type f -name file0 | wc -l)" "0" "expected file0 to not exist"
readDirEntryNames testdir
first_dentry=$(printf "%s\n" "${names%% *}")
assert_eq "file$((count - 2))" "$first_dentry" "expected last dentry to be moved to the first dentry"
check_log_success "Moved dentry from indirect to direct block to fill hole from removed dentry"

# Write stuff to last entry
stuff="STUFF"
last_entry="testdir/file$((count - 2))"
echo "$stuff" >> "$last_entry"
assert_eq "$stuff" "$(cat "$last_entry")" "expected last entry to contain written data"
check_log_success "Can write to indirect block dentries"

# Remove all dentries from last block
dentries_in_last_block=$(((VVSFS_N_DENTRY_PER_BLOCK / 2) - 1))
for (( i = 0; i < dentries_in_last_block; i++)); do
    rm "testdir/file$((direct_and_one_indirect_dentries + i))"
done
readDirEntryNames testdir
last_dentry="${names##* }"
assert_eq "file$((direct_and_one_indirect_dentries - 2))" "$last_dentry" "expected all dentries in last block to be removed"
check_log_success "Removed all dentries from last indirect block and deallocated block"

