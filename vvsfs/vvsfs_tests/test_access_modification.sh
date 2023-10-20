#!/bin/bash
source ./init.sh
log_header "Testing access modification"

expected="1195273980"
touch -d "@$expected" testdir/access_modification_test_file

./remount.sh

# we use epoch instead of pretty print because timezones are hard
output=$(stat testdir/access_modification_test_file -c %X)
assert_eq "$output" "$expected" "access time on file was not updated"
check_log_success "Access time on file is updated"

output=$(stat testdir/access_modification_test_file -c %Y)
assert_eq "$output" "$expected" "last data modification time on file was not updated"
check_log_success "Last data modification time on file is updated"

cat testdir/access_modification_test_file

./remount.sh

output=$(stat testdir/access_modification_test_file -c %X)
assert_not_eq "$output" "$expected" "access time on file did not persist after remount"
check_log_success "Access time on file persisted after remount"

output=$(stat testdir/access_modification_test_file -c %Y)
assert_eq "$output" "$expected" "last data modification time on file did not persist after remount"
check_log_success "Last data modification time on file persisted after remount"
