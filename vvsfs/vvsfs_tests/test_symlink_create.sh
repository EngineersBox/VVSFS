source ./assert.sh

log_header "Testing symlink create"

./create.sh

content="ABCDEFG"

echo "blah" > testdir/aaa
ln -s aaa testdir/bbb
ln -s aaa testdir/ccc
ln -s ccc testdir/ddd
echo "$content" > testdir/bbb

./remount.sh

assert_eq "$content" "$(cat testdir/aaa)" "expected correct content"
check_log_success "Original file persisted data after remount"

assert_eq "$content" "$(cat testdir/bbb)" "expected correct content"
check_log_success "Symlinked file (1/2) can access original file data after remount"

assert_eq "$content" "$(cat testdir/ccc)" "expected correct content"
check_log_success "Symlinked file (2/2) can access original file data after remount"

assert_eq "$content" "$(cat testdir/ddd)" "expected correct content"
check_log_success "Chained symlinked file can access original file data after remount"

assert_eq "'testdir/aaa'" "$(stat testdir/aaa -c %N)" "expected no symlink"
check_log_success "Oringinal file is not a symlink"

assert_eq "'testdir/bbb' -> 'aaa'" "$(stat testdir/bbb -c %N)" "expected link to point correctly"
check_log_success "Symlinked file (1/2) maintins link correctly"

assert_eq "'testdir/ccc' -> 'aaa'" "$(stat testdir/ccc -c %N)" "expected link to point correctly"
check_log_success "Symlinked file (2/2) maintins link correctly"

assert_eq "'testdir/ddd' -> 'ccc'" "$(stat testdir/ddd -c %N)" "expected link to point correctly"
check_log_success "Chained symlinked file maintains link correctly"
