#!/usr/bin/bash

source ./assert.sh
source ./vvsfs_env.sh

log_header "Testing Unlink"

./create.sh

# Setup
content="ABCDEFG"
echo "blah" > testdir/aaa
ln testdir/aaa testdir/bbb
ln testdir/aaa testdir/ccc
ln testdir/ccc testdir/ddd
echo "$content" > testdir/bbb
touch testdir/other.txt

./umount.sh
./mount.sh

# ==== CORE BEHAVIOUR ====

# Remove regular file
rm testdir/other.txt
assert_eq "$(find testdir -type f -name "other.txt" | wc -l)" "0" "expected other.txt to not exist"
assert_eq "$(stat -c %s testdir)" "512" "expected total size == 512"

# Verify files can be resolved and unlinked
assert_eq "$(stat testdir/aaa -c %h)" "4" "expected link count == 4"
rm testdir/ddd
assert_eq "$(stat testdir/aaa -c %h)" "3" "expected link count == 3"
assert_eq "$(stat -c %s testdir)" "384" "expected total size == 384"

./umount.sh
./mount.sh

# Ensure persistence to disk through umount/mount
assert_eq "$(find testdir -type f | wc -l)" "3" "expected 3 files"
assert_eq "$(stat -c %s testdir)" "384" "expected total size == 384" 

# Ensure min link count is kept
rm testdir/ccc testdir/bbb
assert_eq "$(stat testdir/aaa -c %h)" "1" "expected link count == 1"

# ==== BLOCK AND DENTRY HANDLING ====

# Reset
./create.sh

function readDirEntryNames() {
    names=""
    fnames=$(ls -fx $1 | sed "s/[ \t]\+/ /g")
    for entry in "$fnames"; do
        names="$names $entry"
    done
    names=$(echo "$names" | tr -s "[:space:]" | sed "s/^ //g")
}

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
readDirEntryNames testdir
assert_eq "$names" "$expected" "Expected dentry in last block to be moved to first dentry place in first block"

# Non-last dentry in last block
firstDentryInLastBlock=$((dentriesPerBlock + 1))
expected="file$fullBlockAndThreeDentries"
for (( i = 2; i <= dentriesPerBlock; i++ )); do
    expected="$expected file$i"
done
expected="$expected file$((firstDentryInLastBlock + 1))"
rm "testdir/file$firstDentryInLastBlock"
assert_eq "$(find testdir -type f -name "file$firstDentryInLastBlock" | wc -l)" "0" "Expected removed first dentry in last block to not exist"
readDirEntryNames testdir
assert_eq "$names" "$expected" "Expected last dentry in last block to be moved to first dentry in last block"

# Last dentry in last block
firstDentryInLastBlock=$((dentriesPerBlock + 2))
expected="file$fullBlockAndThreeDentries"
for (( i = 2; i <= dentriesPerBlock; i++ )); do
    expected="$expected file$i"
done
rm "testdir/file$firstDentryInLastBlock"
assert_eq "$(find testdir -type f -name "file$firstDentryInLastBlock" | wc -l)" "0" "Expected removed first dentry in last block to not exist"
readDirEntryNames testdir
assert_eq "$names" "$expected" "Expected last dentry in last block to be removed"

