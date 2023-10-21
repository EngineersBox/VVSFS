#!/bin/bash

source ./assert.sh

log_header "Testing moving between different filesystems"

./create.sh

mkdir testdir-otherfs
touch testdir/filea
touch testdir-otherfs/fileb

mv testdir/filea testdir-otherfs/filea2
mv testdir-otherfs/fileb testdir/fileb2

assert_eq "$(ls testdir)" "fileb2" "File was not moved into vvsfs (in memory)"
check_log_success "Move file into VVSFS (create file)"
assert_eq "$(ls testdir-otherfs)" "filea2" "File was not moved out of vvsfs (in memory)"
check_log_success "Move file out of VVSFS (unlink file)"


./umount.sh
./mount.sh

assert_eq "$(ls testdir)" "fileb2" "File was not moved into vvsfs (on disk)"
assert_eq "$(ls testdir-otherfs)" "filea2" "File was not moved out of vvsfs (on disk)"
check_log_success "All renames survived remount"

rm -rf testdir-otherfs
