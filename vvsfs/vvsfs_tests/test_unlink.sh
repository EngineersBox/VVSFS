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

# Verify files can be resolved and unlinked
assert_eq "4" "$(stat testdir/aaa -c %h)" "expected link count == 4"
rm testdir/ddd
assert_eq "3" "$(stat testdir/aaa -c %h)" "expected link count == 3"
assert_eq "640" "$(stat testdir -c %s)" "expected total size == 640"

./umount.sh
./mount.sh

# Ensure persistence to disk through umount/mount
assert_eq "3" "$(find . -type f | wc -l)" "expected 3 files"
assert_eq "512" "$(stat testdir -c %s)" "expected total size == 512" 

# Ensure min link count is kept
rm testdir/ccc testdir/bbb
assert_eq "1" "$(stat testdir/aaa -c %h)" "expected link count == 1"
