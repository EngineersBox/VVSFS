#!/bin/bash
source ./init.sh
log_header "Testing block device"

ZERO_MINOR=$(stat /dev/zero -c %T)
RAND_MINOR=$(stat /dev/random -c %T)
NULL_MINOR=$(stat /dev/null -c %T)

sudo mknod testdir/zero c 1 $ZERO_MINOR
sudo mknod testdir/rand c 1 $RAND_MINOR
sudo mknod testdir/null c 1 $NULL_MINOR

assert_eq "$(stat testdir/zero -c '%F:%t:%T')" "character special file:1:$ZERO_MINOR" "character should have correct major minors"
assert_eq "$(stat testdir/rand -c '%F:%t:%T')" "character special file:1:$RAND_MINOR" "character should have correct major minors"
assert_eq "$(stat testdir/null -c '%F:%t:%T')" "character special file:1:$NULL_MINOR" "character should have correct major minors"

./remount.sh

assert_eq "$(stat testdir/zero -c '%F:%t:%T')" "character special file:1:$ZERO_MINOR" "character should have correct major minors"
assert_eq "$(stat testdir/rand -c '%F:%t:%T')" "character special file:1:$RAND_MINOR" "character should have correct major minors"
assert_eq "$(stat testdir/null -c '%F:%t:%T')" "character special file:1:$NULL_MINOR" "character should have correct major minors"

assert_eq "$(xxd testdir/zero | head -1)" "00000000: 0000 0000 0000 0000 0000 0000 0000 0000  ................" "file should have correct contents"
assert_eq "$(xxd testdir/null)" "" "file should be empty"
