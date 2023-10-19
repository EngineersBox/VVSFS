#!/bin/bash
source ./init.sh
log_header "Testing block device"

## Felix said this is a free ramdisk we can use
sudo mknod testdir/block b 1 1
sudo chmod o+rw testdir/block

assert_eq "$(stat testdir/block -c '%F:%t:%T')" "block special file:1:1" "block should have correct major minors"
echo "AAAA" > testdir/block
assert_eq "$(dd if=testdir/block count=1 bs=5)" "$(echo -e 'AAAA')" "file should have correct contents"

./remount.sh

assert_eq "$(stat testdir/block -c '%F:%t:%T')" "block special file:1:1" "block should have correct major minors"
assert_eq "$(dd if=testdir/block count=1 bs=4)" "$(echo -e 'AAAA')" "file should have correct contents"
echo "BBBB" > testdir/block
assert_eq "$(dd if=testdir/block count=1 bs=4)" "$(echo -e 'BBBB')" "file should have correct contents"
