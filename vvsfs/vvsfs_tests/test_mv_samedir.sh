#!/bin/bash

source ./assert.sh

log_header "Testing moving file to different name in same directory"

./create.sh

echo "aaa" > testdir/filea
mv testdir/filea testdir/fileb

assert_eq "$(ls testdir)" "fileb" "File was not moved (in memory)"
assert_eq "$(cat testdir/fileb)" "aaa" "File contents were not moved (in memory)"

./umount.sh
./mount.sh

assert_eq "$(ls testdir)" "fileb" "File was not moved (on disk)"
assert_eq "$(cat testdir/fileb)" "aaa" "File contents were not moved (on disk)"
