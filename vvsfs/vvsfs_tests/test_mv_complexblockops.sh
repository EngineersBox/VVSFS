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
check_log_success "Rename a file that is not in the last dentry data block succeeds"

mkdir testdir/bfolder
touch testdir/bfolder/block0-file{0..7}
touch testdir/bfolder/block1-file{0..2}
touch testdir/bfolder/block1-file3-last

mv testdir/bfolder/block1-file1 testdir/fileb
assert_eq "$(ls -x testdir)" "afolder  bfolder  filea  fileb" "A file was not successfully moved (last block) (in memory)"
assert_eq "$(ls testdir/bfolder | wc -l)" "11" "A file was not successfully removed after moving (last block) (in memory)"
check_log_success "Renaming the non-last item in the last dentry data block succeeds"

mkdir testdir/cfolder
touch testdir/cfolder/block0-file{0..7}
touch testdir/cfolder/block1-file{0..2}
touch testdir/cfolder/block1-file3-last

mv testdir/cfolder/block1-file3-last testdir/filec
assert_eq "$(ls -x testdir)" "afolder  bfolder  cfolder  filea  fileb  filec" "A file was not successfully moved (last block, last item) (in memory)"
assert_eq "$(ls testdir/cfolder | wc -l)" "11" "A file was not successfully removed after moving (last block, last item) (in memory)"
check_log_success "Renaming the last item in the last dentry data block succeeds"

./umount.sh
./mount.sh

assert_eq "$(ls -x testdir)" "afolder  bfolder  cfolder  filea  fileb  filec" "A file was not successfully moved (on disk)"
assert_eq "$(ls testdir/afolder | wc -l)" "11" "A file was not successfully removed after moving (first block) (on disk)"
assert_eq "$(ls testdir/bfolder | wc -l)" "11" "A file was not successfully removed after moving (last block) (on disk)"
assert_eq "$(ls testdir/cfolder | wc -l)" "11" "A file was not successfully removed after moving (last block, last item) (on disk)"
check_log_success "All renames survive a remount"
