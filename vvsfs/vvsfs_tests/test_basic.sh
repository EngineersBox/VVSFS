#!/bin/bash
source ./init.sh
log_header "Testing basic"

touch testdir/a
echo "AAA" > testdir/as
good_ls=$(ls testdir)

./remount.sh

after_remount=$(ls testdir)
assert_eq "$good_ls" "$after_remount" "expected ls to stay the same"

assert_eq "$(cat testdir/a)" "" "a file should be empty"
assert_eq "$(cat testdir/as)" "AAA" "as file should have correct content"
