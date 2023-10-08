source ./assert.sh

echo Testing access modification

./create.sh

touch -t 0711171533 testdir/access_modification_test_file

./umount.sh
./mount.sh

expected="2007-11-17 15:33:00.000000000 +0000"
output=$(stat testdir/access_modification_test_file -c "%x")
assert_eq "$output" "$expected" "testing access time"
output=$(stat testdir/access_modification_test_file -c "%y")
assert_eq "$output" "$expected" "testing modification time"

cat testdir/access_modification_test_file

./umount.sh
./mount.sh

output=$(stat testdir/access_modification_test_file -c "%x")
assert_not_eq "$output" "$expected" "testing access time"
output=$(stat testdir/access_modification_test_file -c "%y")
assert_eq "$output" "$expected" "testing modification time"
