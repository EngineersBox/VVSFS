#!/bin/bash
source ./init.sh
log_header "Testing access modification"

touch -t 0711171533 testdir/access_modification_test_file

./remount.sh

# we use epoch instead of pretty print because timezones are hard
expected="1195273980"
output=$(stat testdir/access_modification_test_file -c %X)
assert_eq "$output" "$expected" "testing access time"
output=$(stat testdir/access_modification_test_file -c %Y)
assert_eq "$output" "$expected" "testing modification time"

cat testdir/access_modification_test_file

./remount.sh

output=$(stat testdir/access_modification_test_file -c %X)
assert_not_eq "$output" "$expected" "testing access time"
output=$(stat testdir/access_modification_test_file -c %Y)
assert_eq "$output" "$expected" "testing modification time"
