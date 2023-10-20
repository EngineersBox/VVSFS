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
check_log_success "Original file persisted data after remount"

assert_eq "$content" "$(cat testdir/bbb)" "expected correct content"
check_log_success "Hadlinked file (1/2) can access original file data after remount"

assert_eq "$content" "$(cat testdir/ccc)" "expected correct content"
check_log_success "Hadlinked file (2/2) can access original file data after remount"

assert_eq "$content" "$(cat testdir/ddd)" "expected correct content"
check_log_success "Chained hardlinked file can access original file data after remount"

assert_eq "4" "$(stat testdir/aaa -c %h)" "expected link count == 4"
check_log_success "Original file maintains correct link count of 4"
