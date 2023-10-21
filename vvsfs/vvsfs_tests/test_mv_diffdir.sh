#!/bin/bash

source ./assert.sh

log_header "Testing moving file to different directory"

./create.sh

mkdir testdir/afolder

touch testdir/filea
mv testdir/filea testdir/afolder/filea
touch testdir/fileb
mv testdir/fileb testdir/afolder/filec

assert_eq "$(ls -x testdir/afolder)" "filea  filec" "A file was not successfully moved (in memory)"
check_log_success "File was successfully moved to a different directory"

./umount.sh
./mount.sh

assert_eq "$(ls -x testdir/afolder)" "filea  filec" "A file was not successfully moved (on disk)"
check_log_success "Rename survived a remount"
