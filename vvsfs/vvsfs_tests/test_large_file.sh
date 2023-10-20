#!/bin/bash
source ./init.sh
log_header "Testing large file"

dd if=/dev/random count=$(echo $VVSFS_MAXFILESIZE / 512 | bc) bs=512 of=large_random_image.img

cp large_random_image.img testdir/large_random_image.img
assert_eq "$(diff large_random_image.img testdir/large_random_image.img)" "" "the files should be equal"

./remount.sh

assert_eq "$(diff large_random_image.img testdir/large_random_image.img)" "" "the files should still be equal"

# make the file slightly too large
echo A >> large_random_image.img
assert_eq "$(cp large_random_image.img testdir/large_random_image.img 2>&1)" "cp: error writing 'testdir/large_random_image.img': File too large" "the file should be too large to copy in"

### TODO: If we ever fix the fact it overrides the file until the last block (test that the file stayed the same)
