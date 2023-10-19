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
assert_eq "0" "$(find testdir -type d -name dir1 | wc -l)" "empty directory dir1 was not removed"
check_log_success "Empty directories can be removed"

# Can remove nested directories and files
rm -r testdir/dir2
assert_eq "0" "$(find testdir -type d -name dir3 | wc -l)" "nested empty directory dir3 was not removed"
check_log_success "Nested empty directories can be removed"
assert_eq "0" "$(find testdir -type d -name dir2 | wc -l)" "directory containing file was not removed"
check_log_success "Directories with entries can be removed"
assert_eq "0" "$(find testdir -type f -name file1 | wc -l)" "file in removed directory was not removed"
check_log_success "Files within removed directories are also removed"

./remount.sh

# Can remove nested file and then directory
rm testdir/dir4/file2
assert_eq "0" "$(ls testdir/dir4 | wc -l)" "nested directory still has entries after removing nested file"
check_log_success "All entries within directory can be removed"
assert_eq "0" "$(find testdir -type f -name file2 | wc -l)" "nested file in directory was not removed"
check_log_success "Nested file in directory can be removed"
rm -r testdir/dir4
assert_eq "0" "$(find testdir -type d -name dir4 | wc -l)" "parent directory cannot be removed after removing child entries"
check_log_success "Empty directory can be removed after removing entries"


# Can remove nested directory then parent
rm -r testdir/dir5/dir6
assert_eq "0" "$(ls testdir/dir5 | wc -l)" "cannot removed nested directory"
check_log_success "Parent of empty directory that was removed has no entries"
assert_eq "0" "$(find testdir -type d -name dir6 | wc -l)" "expected dir6 to not exist"
check_log_success "Empty nested directory does not exist in parent after removal"
rm -r testdir/dir5
assert_eq "0" "$(find testdir -type d -name dir5 | wc -l)" "parent directory of removed nested child empty directory cannot be removed"
check_log_success "Parent directory of removed child directory can be removed after removing child"
