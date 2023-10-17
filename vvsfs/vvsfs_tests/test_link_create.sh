#!/bin/bash
source ./init.sh
log_header "Testing link create"

content="ABCDEFG"

echo "blah" > testdir/aaa
ln testdir/aaa testdir/bbb
ln testdir/aaa testdir/ccc
ln testdir/ccc testdir/ddd
echo "$content" > testdir/bbb

./remount.sh

assert_eq "$content" "$(cat testdir/aaa)" "expected correct content"
assert_eq "$content" "$(cat testdir/bbb)" "expected correct content"
assert_eq "$content" "$(cat testdir/ccc)" "expected correct content"
assert_eq "$content" "$(cat testdir/ddd)" "expected correct content"

assert_eq "4" "$(stat testdir/aaa -c %h)" "expected link count == 4"
