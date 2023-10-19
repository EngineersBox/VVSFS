#!/bin/bash
source ./init.sh
log_header "Testing basic"

touch testdir/a
echo "AAA" > testdir/as
good_ls=$(ls testdir)

./remount.sh

after_remount=$(ls testdir)
assert_eq "$good_ls" "$after_remount" "expected file system structure to stay the same after remount"
check_log_success "File system structure is persisted after remount"

assert_eq "$(cat testdir/a)" "" "a file should be empty"
check_log_success "Empty file before remount is empty after remount"

assert_eq "$(cat testdir/as)" "AAA" "as file should have correct content"
check_log_success "Data in non-empty file persists after remount"
