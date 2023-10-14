#!/usr/bin/bash

source ./assert.sh

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
assert_eq "0" "$(find testdir -type f -name "other.txt" | wc -l)" "expected other.txt to not exist"
assert_eq "512" "$(stat -c %s testdir)" "expected total size == 512"

# Verify files can be resolved and unlinked
assert_eq "4" "$(stat testdir/aaa -c %h)" "expected link count == 4"
rm testdir/ddd
assert_eq "3" "$(stat testdir/aaa -c %h)" "expected link count == 3"
assert_eq "384" "$(stat -c %s testdir)" "expected total size == 384"

./umount.sh
./mount.sh

# Ensure persistence to disk through umount/mount
assert_eq "3" "$(find testdir -type f | wc -l)" "expected 3 files"
assert_eq "384" "$(stat -c %s testdir)" "expected total size == 384" 

# Ensure min link count is kept
rm testdir/ccc testdir/bbb
assert_eq "1" "$(stat testdir/aaa -c %h)" "expected link count == 1"

# ==== BLOCK AND DENTRY HANDLING ====


