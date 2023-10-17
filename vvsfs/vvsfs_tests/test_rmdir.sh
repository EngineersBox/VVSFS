#!/bin/bash

source ./init.sh

log_header "Testing rmdir"

# Setup
mkdir testdir/dir1
mkdir -p testdir/dir2/dir3
touch testdir/dir2/file1
mkdir testdir/dir4
touch testdir/dir4/file2
mkdir -p testdir/dir5/dir6

# Can remove empty directory
rm -r testdir/dir1
assert_eq "0" "$(find testdir -type d -name dir1 | wc -l)" "expected dir1 to not exist"

# Can remove nested directories and files
rm -r testdir/dir2
assert_eq "0" "$(find testdir -type d -name dir3 | wc -l)" "expected dir3 to not exist"
assert_eq "0" "$(find testdir -type d -name dir2 | wc -l)" "expected dir2 to not exist"
assert_eq "0" "$(find testdir -type f -name file1 | wc -l)" "expected file1 to not exist"

./remount.sh

# Can remove nested file and then directory
rm testdir/dir4/file2
assert_eq "0" "$(ls testdir/dir4 | wc -l)" "expected dir4 to be empty"
assert_eq "0" "$(find testdir -type f -name file2 | wc -l)" "expected file2 to not exist"
rm -r testdir/dir4
assert_eq "0" "$(find testdir -type d -name dir4 | wc -l)" "expected dir4 to not exist"

# Can remove nested directory then parent
rm -r testdir/dir5/dir6
assert_eq "0" "$(ls testdir/dir5 | wc -l)" "expected dir5 to be empty"
assert_eq "0" "$(find testdir -type d -name dir6 | wc -l)" "expected dir6 to not exist"
rm -r testdir/dir5
assert_eq "0" "$(find testdir -type d -name dir5 | wc -l)" "expected dir5 to not exist"
