source ./assert.sh

log_header "Testing whether mv overwrites files/directories as expected"

./create.sh

# DIRECTORIES

mkdir testdir/srcfolder
mkdir testdir/srcfolder/folder
echo "afile" > testdir/srcfolder/folder/afile
mkdir testdir/dstfolder
mkdir testdir/dstfolder/folder
echo "bfile" > testdir/dstfolder/folder/bfile

assert_eq "$(mv testdir/srcfolder/folder testdir/dstfolder 2>&1)" "mv: cannot move 'testdir/srcfolder/folder' to 'testdir/dstfolder/folder': Directory not empty" "mv should generate error when moving to nonempty dir"
assert_eq "$(ls -x testdir/dstfolder/folder)" "bfile" "Should not overwrite a folder which contains files"
check_log_success "Renaming a folder to a non-empty destination folder should fail"
assert_eq "$(ls -x testdir/srcfolder)" "folder" "Should not delete folder on failed move"
assert_eq "$(ls -x testdir/srcfolder/folder)" "afile" "Should not delete folder contents on failed move"
check_log_success "A failed move should not delete the original source file"

rm testdir/dstfolder/folder/bfile
mv testdir/srcfolder/folder testdir/dstfolder
assert_eq "$(ls -x testdir/dstfolder/folder)" "afile" "Should overwrite an empty folder (in memory)"
check_log_success "Should be possible to overwrite an empty folder"
assert_eq "$(ls -x testdir/srcfolder)" "" "Should delete folder on successful move (in memory)"
check_log_success "After a successful move, the src folder should be deleted"

# Adding this back in fixes the test
# sync

# FILES

mkdir testdir/files
echo "aaa" > testdir/files/a
echo "bbb" > testdir/files/b
mv testdir/files/a testdir/files/b
assert_eq "$(<testdir/files/b)" "aaa" "Files should overwrite each other (in memory)"
check_log_success "Files should be able to overwrite files"


# FILES & DIRECTORIES

mkdir -p testdir/another/a
mkdir -p testdir/another/b
touch testdir/another/b/a

assert_eq "$(mv testdir/another/a testdir/another/b 2>&1)" "mv: cannot overwrite non-directory 'testdir/another/b/a' with directory 'testdir/another/a'" "Folder should not overwrite file (in memory)"
check_log_success "Directories should not be able to overwrite files"

assert_eq "$(mv testdir/another/b/a testdir/another 2>&1)" "mv: cannot overwrite directory 'testdir/another/a' with non-directory" "File should not overwrite folder (in memory)"
check_log_success "Files should not be able to overwrite directories"

./remount.sh

assert_eq "$(ls -x testdir/dstfolder/folder)" "afile" "Should overwrite an empty folder (on disk)"
assert_eq "$(ls -x testdir/srcfolder)" "" "Should delete folder on successful move (on disk)"

# assert_eq "$(<testdir/files/b)" "aaa" "Files should overwrite each other (on disk)"

check_log_success "All renames survive a remount"
