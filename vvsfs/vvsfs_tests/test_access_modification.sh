#!/bin/bash
source ./init.sh
log_header "Testing access modification"

touch -t 0711171533 testdir/access_modification_test_file

./remount.sh

expected="2007-11-17 15:33:00.000000000 +0000"
output=$(stat testdir/access_modification_test_file -c "%x")
assert_eq "$output" "$expected" "testing access time"
output=$(stat testdir/access_modification_test_file -c "%y")
assert_eq "$output" "$expected" "testing modification time"

cat testdir/access_modification_test_file

./remount.sh

output=$(stat testdir/access_modification_test_file -c "%x")
assert_not_eq "$output" "$expected" "testing access time"
output=$(stat testdir/access_modification_test_file -c "%y")
assert_eq "$output" "$expected" "testing modification time"
