#!/bin/bash

source init.sh

log_header "Test inode indirect pointers"

# Fill into indirect pointer block
direct_and_one_indirect_dentries=$((VVSFS_N_DENTRY_PER_BLOCK * VVSFS_N_BLOCKS))
# Includes 1.5 indirect blocks
count=$(($direct_and_one_indirect_dentries + (VVSFS_N_DENTRY_PER_BLOCK / 2)))
for (( i = 0; i < count; i++)); do
    echo "Creating: testdir/file$i"
    touch "testdir/file$i"
    #assert_eq "$(find testdir -type f -name "file$i" | wc -l)" "1" "expected file1 to exist"
done

## Ensure that unlinks can move blocks from indirect to direct
#rm testdir/file0
#assert_eq "$(find testdir -type f -name file0 | wc -l)" "0" "expected file0 to not exist"
#readDirEntryNames testdir
#first_dentry=$(printf "%s\n" "${names%% *}")
#assert_eq "file$((count - 1))" "$first_dentry" "expected last dentry to be moved to the first dentry"

## Write stuff to last entry
#stuff="STUFF"
#last_entry="testdir/file$((count - 2))"
#echo "$stuff" >> "$last_entry"
#assert_eq "$stuff" "$(cat "$last_entry")" "expected last entry to contain written data"

## Remove all dentries from last block
#dentries_in_last_block=$(((VVSFS_N_DENTRY_PER_BLOCK / 2) - 1))
#for (( i = 0; i < dentries_in_last_block; i++)); do
    #rm "testdir/file$((direct_and_one_indirect_dentries - 1 + i))"
#done
#readDirEntryNames testdir
#last_dentry="${names##* }"
#assert_eq "file$((direct_and_one_indirect_dentries - 1))" "$last_dentry" "expected all dentries in last block to be removed"
