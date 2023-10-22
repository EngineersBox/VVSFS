#!/bin/bash
source ./init.sh
log_header "Testing unlink of hardlinks"

CONTENT="aaaaaa"
CONTENT2="bbbbbb"

echo $CONTENT > testdir/a
link testdir/a testdir/b

# remove the first file this should not deallocate the inode because b still points there
rm testdir/a

# try writing several files to ensure they don't overwrite "b"
for i in {0..100}; do
    echo $CONTENT2 > testdir/overwrite_$i
done

./remount.sh

assert_eq "$(<testdir/b)" $CONTENT "file shouldn't have been deallocated"
check_log_success "Hard linked file still has content after a single unlink"
