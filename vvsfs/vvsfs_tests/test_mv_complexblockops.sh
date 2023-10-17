#!/bin/bash

source ./assert.sh

log_header "Testing moving file to different directory, with complex block operations required"

./create.sh

mkdir testdir/afolder
touch testdir/afolder/block0-file{0..7}
touch testdir/afolder/block1-file{0..2}
touch testdir/afolder/block1-file3-last

mv testdir/afolder/block0-file6 testdir/filea
assert_eq "$(ls -x testdir)" "afolder  filea" "A file was not successfully moved (first block) (in memory)"
assert_eq "$(ls testdir/afolder | wc -l)" "11" "A file was not successfully removed after moving (first block) (in memory)"

mkdir testdir/bfolder
touch testdir/bfolder/block0-file{0..7}
touch testdir/bfolder/block1-file{0..2}
touch testdir/bfolder/block1-file3-last

mv testdir/bfolder/block1-file1 testdir/fileb
assert_eq "$(ls -x testdir)" "afolder  bfolder  filea  fileb" "A file was not successfully moved (last block) (in memory)"
assert_eq "$(ls testdir/bfolder | wc -l)" "11" "A file was not successfully removed after moving (last block) (in memory)"

mkdir testdir/cfolder
touch testdir/cfolder/block0-file{0..7}
touch testdir/cfolder/block1-file{0..2}
touch testdir/cfolder/block1-file3-last

mv testdir/cfolder/block1-file3-last testdir/filec
assert_eq "$(ls -x testdir)" "afolder  bfolder  cfolder  filea  fileb  filec" "A file was not successfully moved (last block, last item) (in memory)"
assert_eq "$(ls testdir/cfolder | wc -l)" "11" "A file was not successfully removed after moving (last block, last item) (in memory)"

mkdir testdir/srcfolder
mkdir testdir/srcfolder/folder
touch testdir/srcfolder/folder/afile
mkdir testdir/dstfolder
mkdir testdir/dstfolder/folder
touch testdir/dstfolder/folder/bfile

assert_eq "$(mv testdir/srcfolder/folder testdir/dstfolder 2>&1)" "mv: cannot move 'testdir/srcfolder/folder' to 'testdir/dstfolder/folder': Directory not empty" "mv should generate error when moving to nonempty dir"
assert_eq "$(ls -x testdir/dstfolder/folder)" "bfile" "Should not overwrite a folder which contains files"
assert_eq "$(ls -x testdir/srcfolder)" "folder" "Should not delete folder on failed move"
assert_eq "$(ls -x testdir/srcfolder/folder)" "afile" "Should not delete folder contents on failed move"

rm testdir/dstfolder/folder/bfile
mv testdir/srcfolder/folder testdir/dstfolder
assert_eq "$(ls -x testdir/dstfolder/folder)" "afile" "Should overwrite an empty folder (in memory)"
assert_eq "$(ls -x testdir/srcfolder)" "" "Should delete folder on successful move (in memory)"

./umount.sh
./mount.sh

assert_eq "$(ls -x testdir)" "afolder  bfolder  cfolder  dstfolder  filea  fileb  filec  srcfolder" "A file was not successfully moved (on disk)"
assert_eq "$(ls testdir/afolder | wc -l)" "11" "A file was not successfully removed after moving (first block) (on disk)"
assert_eq "$(ls testdir/bfolder | wc -l)" "11" "A file was not successfully removed after moving (last block) (on disk)"
assert_eq "$(ls testdir/cfolder | wc -l)" "11" "A file was not successfully removed after moving (last block, last item) (on disk)"
assert_eq "$(ls -x testdir/dstfolder/folder)" "afile" "Should overwrite an empty folder (on disk)"
assert_eq "$(ls -x testdir/srcfolder)" "" "Should delete folder on successful move (on disk)"
