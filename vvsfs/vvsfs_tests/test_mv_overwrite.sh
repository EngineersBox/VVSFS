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
assert_eq "$(ls -x testdir/srcfolder)" "folder" "Should not delete folder on failed move"
assert_eq "$(ls -x testdir/srcfolder/folder)" "afile" "Should not delete folder contents on failed move"

rm testdir/dstfolder/folder/bfile
mv testdir/srcfolder/folder testdir/dstfolder
assert_eq "$(ls -x testdir/dstfolder/folder)" "afile" "Should overwrite an empty folder (in memory)"
assert_eq "$(ls -x testdir/srcfolder)" "" "Should delete folder on successful move (in memory)"

# Adding this back in fixes the test
# sync

# FILES

mkdir testdir/files
echo "aaa" > testdir/files/a
echo "bbb" > testdir/files/b
mv testdir/files/a testdir/files/b
assert_eq "$(<testdir/files/b)" "aaa" "Files should overwrite each other (in memory)"

./remount.sh

assert_eq "$(ls -x testdir/dstfolder/folder)" "afile" "Should overwrite an empty folder (on disk)"
assert_eq "$(ls -x testdir/srcfolder)" "" "Should delete folder on successful move (on disk)"

# assert_eq "$(<testdir/files/b)" "aaa" "Files should overwrite each other (on disk)"
